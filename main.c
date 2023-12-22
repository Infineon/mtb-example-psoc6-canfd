/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the CANFD example
*
* Related Document: See README.md
*
*******************************************************************************
* Copyright 2023, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#include <stdio.h>
#include <string.h>
#include "cyhal.h"
#include "cy_pdl.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

/*******************************************************************************
* Macros
*******************************************************************************/
/* CAN-FD message identifier 1*/
#define CANFD_NODE_1            1
/* CAN-FD message identifier 2 (use different for 2nd device) */
#define CANFD_NODE_2            2
/* message Identifier used for this code */
#define USE_CANFD_NODE          CANFD_NODE_1
/* CAN-FD channel number used */
#define CANFD_HW_CHANNEL        0
/* CAN-FD data buffer index to send data from */
#define CANFD_BUFFER_INDEX      0
/* Maximum incoming data length supported */
#define CANFD_DLC               8

#define CANFD_INTERRUPT         canfd_0_interrupts0_0_IRQn

#define GPIO_INTERRUPT_PRIORITY (7u)

/*******************************************************************************
* Global Variables
*******************************************************************************/
/* This is a shared context structure, unique for each can-fd channel */
static cy_stc_canfd_context_t canfd_context;

/* Variable which holds the button pressed status */
volatile bool gpio_intr_flag = false;

cyhal_gpio_callback_data_t gpio_btn_callback_data;

/* Populate the configuration structure for CAN-FD Interrupt */
cy_stc_sysint_t canfd_irq_cfg =
{
    /* Source of interrupt signal */
    .intrSrc = CANFD_INTERRUPT,
    /* Interrupt priority */
    .intrPriority = 1U,
};

/*******************************************************************************
* Function Prototypes
*******************************************************************************/

/* can-fd interrupt handler */
static void isr_canfd (void);

/* button press interrupt handler */
static void gpio_interrupt_handler(void *handler_arg, cyhal_gpio_event_t event);

/* handler for general errors */
void handle_error(uint32_t status);

/*******************************************************************************
* Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function. It initializes the CAN-FD channel and interrupt.
* User button and User LED are also initialized. The main loop checks for the
* button pressed interrupt flag and when it is set, a CAN-FD frame is sent.
* Whenever a CAN-FD frame is received from other nodes, the user LED toggles and
* the received data is logged over serial terminal.
*
* Parameters:
*  none
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    cy_en_canfd_status_t status;
    /* Initialize the device and board peripherals */
    result = cybsp_init();
    /* Board init failed. Stop program execution */
    handle_error(result);
    /* Initialize retarget-io for uart logging */
    result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                                CY_RETARGET_IO_BAUDRATE);
    /* Retarget-io init failed. Stop program execution */
    handle_error(result);

    printf("===========================================================\r\n");
    printf("Welcome to CAN-FD example\r\n");
    printf("===========================================================\r\n\n");

    printf("===========================================================\r\n");
    printf("CAN-FD Node-%d (message id)\r\n", USE_CANFD_NODE);
    printf("===========================================================\r\n\n");

    /* Hook the interrupt service routine */
    (void) Cy_SysInt_Init(&canfd_irq_cfg, &isr_canfd);
    /* enable the CAN-FD interrupt */
    NVIC_EnableIRQ(CANFD_INTERRUPT);

    /* Initialize the user LED */
    result = cyhal_gpio_init(CYBSP_USER_LED, CYHAL_GPIO_DIR_OUTPUT,
                    CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);
    /* User LED init failed. Stop program execution */
    handle_error(result);

    /* Initialize the user button */
    result = cyhal_gpio_init(CYBSP_USER_BTN, CYHAL_GPIO_DIR_INPUT,
                    CYBSP_USER_BTN_DRIVE, CYBSP_BTN_OFF);
    /* User button init failed. Stop program execution */
    handle_error(result);

    /* Configure GPIO interrupt */
    gpio_btn_callback_data.callback = gpio_interrupt_handler;
    cyhal_gpio_register_callback(CYBSP_USER_BTN,
                                 &gpio_btn_callback_data);
    cyhal_gpio_enable_event(CYBSP_USER_BTN, CYHAL_GPIO_IRQ_FALL,
                                     GPIO_INTERRUPT_PRIORITY, true);

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize CAN-FD Channel */
    status = Cy_CANFD_Init(CANFD_HW, CANFD_HW_CHANNEL, &CANFD_config,
                           &canfd_context);

    handle_error(status);

    /* Setting Node(message) Identifier to global setting of "USE_CANFD_NODE" */
    CANFD_T0RegisterBuffer_0.id = USE_CANFD_NODE;

    for(;;)
    {
        if (true == gpio_intr_flag)
        {
            /* Sending CAN-FD frame to other node */
            status = Cy_CANFD_UpdateAndTransmitMsgBuffer(CANFD_HW,
                                                    CANFD_HW_CHANNEL,
                                                    &CANFD_txBuffer_0,
                                                    CANFD_BUFFER_INDEX,
                                                    &canfd_context);
            if(CY_CANFD_SUCCESS == status)
            {
                printf("CAN-FD Frame sent with message ID-%d\r\n\r\n",
                        USE_CANFD_NODE);
            }
            else
            {
                printf("Error sending CAN-FD Frame with message ID-%d\r\n\r\n",
                        USE_CANFD_NODE);
            }

            gpio_intr_flag = false;
        }
    }
}

/*******************************************************************************
* Function Name: gpio_interrupt_handler
********************************************************************************
* Summary:
*   GPIO interrupt handler.
*
* Parameters:
*  void *handler_arg (unused)
*  cyhal_gpio_event_t (unused)
*
*******************************************************************************/
static void gpio_interrupt_handler(void *handler_arg, cyhal_gpio_event_t event)
{
    gpio_intr_flag = true;
}

/*******************************************************************************
* Function Name: isr_canfd
********************************************************************************
* Summary:
* This is the interrupt handler function for the can-fd interrupt.
*
* Parameters:
*  none
*
*******************************************************************************/
static void isr_canfd(void)
{
    /* Just call the IRQ handler with the current channel number and context */
    Cy_CANFD_IrqHandler(CANFD_HW, CANFD_HW_CHANNEL, &canfd_context);
}

/*******************************************************************************
* Function Name: canfd_rx_callback
********************************************************************************
* Summary:
* This is the callback function for can-fd reception
*
* Parameters:
*    msg_valid                     Message received properly or not
*    msg_buf_fifo_num              RxFIFO number of the received message
*    canfd_rx_buf                  Message buffer
*
*******************************************************************************/
void canfd_rx_callback (bool  msg_valid, uint8_t msg_buf_fifo_num,
                        cy_stc_canfd_rx_buffer_t* canfd_rx_buf)
{
    /* Array to hold the data bytes of the CAN-FD frame */
    uint8_t canfd_data_buffer[CANFD_DLC];
    /* Variable to hold the data length code of the CAN-FD frame */
    uint32_t canfd_dlc;
    /* Variable to hold the Identifier of the CAN-FD frame */
    uint32_t canfd_id;

    if (true == msg_valid)
    {
        /* Checking whether the frame received is a data frame */
        if(CY_CANFD_RTR_DATA_FRAME == canfd_rx_buf->r0_f->rtr)
        {

            cyhal_gpio_toggle(CYBSP_USER_LED);

            canfd_dlc = canfd_rx_buf->r1_f->dlc;
            canfd_id  = canfd_rx_buf->r0_f->id;

            printf("%d bytes received with message identifier %d\r\n\r\n",
                                                        (int)canfd_dlc,
                                                        (int)canfd_id);

            memcpy(canfd_data_buffer,canfd_rx_buf->data_area_f,canfd_dlc);

            printf("Rx Data : ");

            for (uint8_t msg_idx = 0U; msg_idx < canfd_dlc ; msg_idx++)
            {
                printf(" %d ", canfd_data_buffer[msg_idx]);
            }

            printf("\r\n\r\n");
        }
    }
}

/*******************************************************************************
* Function Name: handle_error
********************************************************************************
*
* Summary:
* User defined error handling function. This function processes unrecoverable
* errors such as any initialization errors etc. In case of such error the system
* will enter into assert.
*
* Parameters:
*  uint32_t status - status indicates success or failure
*
* Return:
*  void
*
*******************************************************************************/
void handle_error(uint32_t status)
{
    if (status != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }
}

/* [] END OF FILE */
