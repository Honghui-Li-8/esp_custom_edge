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
#include <time.h>
#include "board.h"

#ifdef LOCAL_EDGE_DEVICE_ENABLED
#include "local_edge_device.c"
#endif

#define TAG_B "BOARD"
#define TAG_W "Debug"

extern void send_message(uint16_t dst_address, uint16_t length, uint8_t *data_ptr, bool require_response);
extern void printNetworkInfo();

clock_t start_time;
bool timeout = false;

void startTimer() {
    start_time = clock();
}

void setTimeout(bool boolean) {
    timeout = boolean;
}

double getTimeElapsed() {
    clock_t end_time = clock();
    return ((double) (end_time - start_time)) / CLOCKS_PER_SEC;
}

bool getTimeout() {
    return timeout;
}

void setLEDState(enum State nodeState) {
    if(nodeState == DISCONNECTED) {
        board_led_operation(50, 0, 0); // Red LED Color
    }
    else if (nodeState == CONNECTING) {
        board_led_operation(0, 0, 50); // Blue LED Color
    }
    else if (nodeState ==  CONNECTED) {
        board_led_operation(0, 50, 0); // Green LED Color
    }
    else if (nodeState == WORKING) {
        board_led_operation(50, 50, 0); // Yellow LED Color
    }
    else {
        board_led_operation(0, 0, 0); // No Color == No State
    }
}

void board_led_operation(uint8_t r, uint8_t g, uint8_t b)
{
    rmt_led_set(r,g,b);
}

static void board_led_init(void)
{
    rmt_encoder_init();
}

// ====================== repetive code, better clean up ======================
void board_dispatch_network_command(char *ble_cmd, uint16_t node_addr, uint8_t *data_buffer, size_t data_length)
{
    uint8_t command_msg[MAX_MSG_LEN + BLE_CMD_LEN + BLE_ADDR_LEN];
    memset(command_msg, 0, MAX_MSG_LEN + BLE_CMD_LEN + BLE_ADDR_LEN);
    uint8_t *msg_itr = command_msg;
    uint16_t node_addr_network_order = htons(node_addr);

    if (data_length > MAX_MSG_LEN)
    {
        ESP_LOGE(TAG_L, "Local Edge Device Trying to Send %d bytes message that's more than MAX_MSG_LEN-%d", (int)data_length, (int)MAX_MSG_LEN);
        return;
    }

    memcpy(msg_itr, ble_cmd, BLE_CMD_LEN);
    msg_itr += BLE_CMD_LEN;
    memcpy(msg_itr, &node_addr_network_order, BLE_ADDR_LEN);
    msg_itr += BLE_ADDR_LEN;

    if (data_buffer != NULL)
    {
        memcpy(msg_itr, data_buffer, data_length);
        msg_itr += data_length;
    }

    ESP_LOGI(TAG_L, "data_buffer: '%.*s'", data_length, data_buffer);
    execute_network_command((char *)command_msg, msg_itr - command_msg);
}

void board_ble_send_to_root(uint8_t *data_buffer, size_t data_length)
{
    char ble_cmd[7] = "SEND-";
    ESP_LOGI(TAG_L, "data_buffer: '%.*s'", data_length, data_buffer);
    board_dispatch_network_command(ble_cmd, 0, data_buffer, data_length);
}

// ====================== repetive code, better clean up ======================

static void button_tap_cb(void* arg)
{
    ESP_LOGW(TAG_W, "button pressed ------------------------- ");

    ESP_LOGW(TAG_W, "sending------");
    static int control = 0;

    if (control == 0) {
        char data[20] = "[M] Hello";
        send_message(PROV_OWN_ADDR, strlen(data), (uint8_t *)data, false);
        control = 1;
    } else {
        char data[20] = "[D]GPS6------";
        data[3] = (char) 0x06; // 6 byte GPS data

        send_message(PROV_OWN_ADDR, strlen(data), (uint8_t *)data, false);
        control = 0;
    }

    ESP_LOGW(TAG_W, "sended-------");

}

static void button_liong_press_cb(void *arg)
{
    ESP_LOGW(TAG_W, "button long pressed ------------------------- ");
    ESP_LOGW(TAG_W, "sending robot reqyest------");
    uint8_t buffer[10];
    uint8_t *buf_itr = buffer;

    // message type
    strncpy((char *)buf_itr, "REQ", 3);
    buf_itr += 3;

    // request type
    strncpy((char *)buf_itr, "R", 1);
    buf_itr += 1;

    board_ble_send_to_root(buffer, buf_itr - buffer);
}

static void board_button_init(void)
{
    button_handle_t btn_handle = iot_button_create(BUTTON_IO_NUM, BUTTON_ACTIVE_LEVEL);
    if (btn_handle) {
        iot_button_set_evt_cb(btn_handle, BUTTON_CB_RELEASE, button_tap_cb, "RELEASE");
        iot_button_set_serial_cb(btn_handle, 3, 5000, button_liong_press_cb, "SERIAL");
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
int uart_write_encoded_bytes(uart_port_t uart_num, uint8_t* data, size_t length) {
    uint8_t esacpe_byte = ESCAPE_BYTE;

    int byte_wrote = 0;
    for (uint8_t* byte_itr = data; byte_itr < data + length; ++byte_itr) {
        if (byte_itr[0] < esacpe_byte) {
            uart_write_bytes(UART_NUM, byte_itr, 1);
            byte_wrote += 1;
            continue;
        }

        // nned 2 byte encoded
        uint8_t encoded = byte_itr[0] ^ esacpe_byte; // bitwise Xor
        uart_write_bytes(UART_NUM, &esacpe_byte, 1);
        uart_write_bytes(UART_NUM, &encoded, 1);
        byte_wrote += 2;
    }

    return byte_wrote;
}

// Able to wrote back to the same buffer, since decoded data is always shorter
int uart_decoded_bytes(uint8_t* data, size_t length, uint8_t* decoded_data) {
    int decoed_len = 0;
    uint8_t* decode_itr = decoded_data;

    for (uint8_t* byte_itr = data; byte_itr < data + length; ++byte_itr) {
        if (byte_itr[0] != ESCAPE_BYTE) {
            // not a ESCAPE_BYTE
            decode_itr[0] = byte_itr[0];
            decode_itr += 1;
            decoed_len += 1;
            continue;
        }

        // ESCAPE_BYTE, decode 2 byte into 1
        byte_itr += 1; // move to next to get encoded byte
        uint8_t encoded = byte_itr[0];
        
        uint8_t decoded = encoded ^ ESCAPE_BYTE; // bitwise Xor
        decode_itr[0] = decoded;
        decode_itr += 1;
        decoed_len += 1;
    }
    
    return decoed_len;
}


// TB Finish, need to encode the send data for escape bytes
// do we need to regulate the message length?
int uart_sendData(uint16_t node_addr, uint8_t* data, size_t length)
{
#ifdef LOCAL_EDGE_DEVICE_ENABLED
    // enabled local_edge_device, pass message to local_edge_device
    local_edge_device_network_message_handler(node_addr, data, length);
    return length;
#else
    // not enabled local_edge_device, pass message to uart with uart encoding
    uint8_t uart_start = UART_START;
    uint8_t uart_end = UART_END;
    int txBytes = 0;

    uint16_t node_addr_big_endian = htons(node_addr); 
    txBytes += uart_write_bytes(UART_NUM, &uart_start, 1); // 0xFF
    txBytes += uart_write_encoded_bytes(UART_NUM, (uint8_t*) &node_addr_big_endian, 2);
    txBytes += uart_write_encoded_bytes(UART_NUM, data, length);
    txBytes += uart_write_bytes(UART_NUM, &uart_end, 1);  // 0xFE

    ESP_LOGI("[UART]", "Wrote %d bytes Data on uart-tx", txBytes);
    return txBytes;
#endif
}

// TB Finish, need to encode the send data for escape bytes
int uart_sendMsg(uint16_t node_addr, char* msg)
{
    size_t length = strlen(msg);
    uint8_t uart_start = UART_START;
    uint8_t uart_end = UART_END;
    int txBytes = 0;

    uint16_t node_addr_big_endian = htons(node_addr); 
    txBytes += uart_write_bytes(UART_NUM, &uart_start, 1); // 0xFF
    txBytes += uart_write_encoded_bytes(UART_NUM, (uint8_t*) &node_addr_big_endian, 2);
    txBytes += uart_write_encoded_bytes(UART_NUM, (uint8_t*) msg, length);
    txBytes += uart_write_bytes(UART_NUM, &uart_end, 1);  // 0xFE

    ESP_LOGI("[UART]", "Wrote %d bytes Msg on uart-tx", txBytes);
    return txBytes;
}

void board_init(void)
{
    uart_init();
    board_led_init();
    board_button_init();

#ifdef LOCAL_EDGE_DEVICE_ENABLED
    // enabled local_edge_device, initialize the local device
    local_edge_device_init();
#endif
}
