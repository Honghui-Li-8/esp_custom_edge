/* board.c - Board-specific hooks */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "esp_log.h"
#include "iot_button.h"
#include <string.h>
#include "board.h"

#define TAG_B "BOARD"
#define TAG_W "Debug"

#define BUTTON_IO_NUM           9
#define BUTTON_ACTIVE_LEVEL     0
#define ESCAPE_BYTE 0xFA

extern void send_message(uint16_t dst_address, uint16_t length, uint8_t *data_ptr);
extern void printNetworkInfo();

static void button_tap_cb(void* arg)
{
    ESP_LOGW(TAG_W, "button pressed ------------------------- ");
    // static uint8_t *data_buffer = NULL;
    // if (data_buffer == NULL) {
    //     data_buffer = (uint8_t*)malloc(128);
    //     if (data_buffer == NULL) {
    //         printf("Memory allocation failed.\n");
    //         return;
    //     }
    // }
    
    // strcpy((char*)data_buffer, "hello world, this is Edge");
    // send_message(0x0001, strlen("hello world, this is Edge") + 1, data_buffer);
    static int state = 1;
    if (state == 1) {
        gpio_set_level(GPIO_NUM_8, 1);
        ESP_LOGW(TAG_W, "---ON---");
        state = 0;
    }
    else {
        gpio_set_level(GPIO_NUM_8, 0);
        ESP_LOGW(TAG_W, "---OFF---");
        state = 1;
    }
}

static void board_button_init(void)
{
    button_handle_t btn_handle = iot_button_create(BUTTON_IO_NUM, BUTTON_ACTIVE_LEVEL);
    if (btn_handle) {
        iot_button_set_evt_cb(btn_handle, BUTTON_CB_RELEASE, button_tap_cb, "RELEASE");
    }
}

static void uart_init() {  // Uart ===========================================================
    const int uart_num = UART_NUM;
    const int uart_buffer_size = UART_BUF_SIZE * 2;
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl= UART_HW_FLOWCTRL_DISABLE, // = UART_HW_FLOWCTRL_CTS_RTS,
        .rx_flow_ctrl_thresh = UART_SCLK_DEFAULT, // = 122,
    };

    ESP_ERROR_CHECK(uart_driver_install(uart_num, uart_buffer_size,
                                        uart_buffer_size, 0, NULL, 0)); // not using queue
                                        // uart_buffer_size, 20, &uart_queue, 0));
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    // Set UART pins                      (TX,      RX,      RTS,     CTS)
    ESP_ERROR_CHECK(uart_set_pin(uart_num, TXD_PIN, RXD_PIN, RTS_PIN, CTS_PIN));

    ESP_LOGI(TAG_B, "Uart init done");
}

// escape char
int uart_write_encoded_bytes(uart_port_t uart_num, const uint8_t* data, size_t length) {
    int byte_wrote = 0;
    for (uint8_t* byte_itr = data; byte_itr < data + length; ++byte_itr) {
        if (byte_itr[0] < ESCAPE_BYTE) {
            uart_write_bytes(UART_NUM, byte_itr, 1);
            byte_wrote += 1;
            continue;
        }

        // nned 2 byte encoded
        uint8_t encoded = byte_itr[0] ^ ESCAPE_BYTE // bitwise Xor
        uart_write_bytes(UART_NUM, &ESCAPE_BYTE, 1);
        uart_write_bytes(UART_NUM, &encoded, 1);
        byte_wrote += 2;
    }
}

// Able to wrote back to the same buffer, since decoded data is always shorter
size_t uart_decoded_bytes(const uint8_t* data, size_t length, const uint8_t* decoded_data) {
    int decoed_len = 0;
    uint8_t* decode_itr = decoded_data;

    // decode_itr always <= byte_itr since decode message always shorter
    for (uint8_t* byte_itr = data; byte_itr < data + length; ++byte_itr) {
        if (byte_itr[0] != ESCAPE_BYTE) {
            // not a ESCAPE_BYTE
            decode_itr[0] = byte_itr[0];
            decode_itr += 1;
            decoed_len += 1;
            continue;
        }

        // ESCAPE_BYTE, decode 2 byte into 1
        uint8_t encoded = byte_itr[1];
        byte_itr += 1;
        
        uint8_t decoded = encoded ^ ESCAPE_BYTE // bitwise Xor
        decode_itr[0] = decoded;
        decode_itr += 1;
        decoed_len += 1;
    }
    
    return decoed_len;
}


// TB Finish, need to encode the send data for escape bytes
int uart_sendData(uint16_t node_addr, const uint8_t* data, size_t length)
{
    int txBytes = 0;
    unsigned char uart_start = 0xFF; // start of uart message
    unsigned char uart_end = 0xFE;   // end of uart message

    uint16_t node_addr_big_endian = htons(num); 
    uart_write_bytes(UART_NUM, &uart_start, 1);
    txBytes += uart_write_encoded_bytes(UART_NUM, &node_addr_big_endian, 2);
    txBytes += uart_write_encoded_bytes(UART_NUM, data, length);
    uart_write_bytes(UART_NUM, &uart_end, 1);

    ESP_LOGI("[UART]", "Wrote %d bytes Data on uart-tx", txBytes);
    return txBytes;
}

// TB Finish, need to encode the send data for escape bytes
int uart_sendMsg(uint16_t node_addr, const char* msg)
{
    size_t length = strlen(msg);
    int txBytes = uart_write_bytes(UART_NUM, msg, length);
    txBytes += uart_sendEndOfMsg(); // end of message symbol

    ESP_LOGI("[UART]", "Wrote %d bytes Msg on uart-tx", txBytes);
    return txBytes;
}

void board_init(void)
{
    uart_init();
    board_button_init();
}
