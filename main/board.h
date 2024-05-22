/* board.h - Board-specific hooks */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _BOARD_H_
#define _BOARD_H_

#include "driver/uart.h"
#include "driver/gpio.h"
#include <arpa/inet.h>
#include "../Secret/NetworkConfig.h"

// Dev mode send uart signal to usb-uart port
// #define UART_NUM_H2 UART_NUM_0 // defult log port
// #define TX_PIN_H2 24 // dpin connected with usb-uart
// #define RX_PIN_H2 23 // dpin connected with usb-uart

#define UART_NUM_H2 UART_NUM_1 // UART_NUM_0 is used for usb monitor already
#define TX_PIN_H2 0 // we can define any gpio pin
#define RX_PIN_H2 1 // we can define any gpio pin

#define UART_NUM    UART_NUM_H2
#define TXD_PIN     TX_PIN_H2
#define RXD_PIN     RX_PIN_H2
#define RTS_PIN     UART_PIN_NO_CHANGE // not using
#define CTS_PIN     UART_PIN_NO_CHANGE // not using
#define UART_BAUD_RATE 115200
#define UART_BUF_SIZE 1024

#define BUTTON_IO_NUM           9
#define BUTTON_ACTIVE_LEVEL     0
#define ESCAPE_BYTE 0xFA
#define UART_START 0xFF
#define UART_END 0xFE

void board_init(void);
int uart_write_encoded_bytes(uart_port_t uart_num, uint8_t* data, size_t length);
int uart_decoded_bytes(uint8_t* data, size_t length, uint8_t* decoded_data);
int uart_sendData(uint16_t node_addr, uint8_t* data, size_t length);
int uart_sendMsg(uint16_t node_addr, char* msg);

// #if defined(CONFIG_BLE_MESH_ESP_WROOM_32)
// #define LED_R GPIO_NUM_25
// #define LED_G GPIO_NUM_26
// #define LED_B GPIO_NUM_27
// #elif defined(CONFIG_BLE_MESH_ESP_WROVER)
// #define LED_R GPIO_NUM_0
// #define LED_G GPIO_NUM_2
// #define LED_B GPIO_NUM_4
// #elif defined(CONFIG_BLE_MESH_ESP32C3_DEV)
// #define LED_R GPIO_NUM_8
// #define LED_G GPIO_NUM_8
// #define LED_B GPIO_NUM_8
// #elif defined(CONFIG_BLE_MESH_ESP32S3_DEV)
// #define LED_R GPIO_NUM_47
// #define LED_G GPIO_NUM_47
// #define LED_B GPIO_NUM_47
// #elif defined(CONFIG_BLE_MESH_ESP32C6_DEV)
// #define LED_R GPIO_NUM_8
// #define LED_G GPIO_NUM_8
// #define LED_B GPIO_NUM_8
// #elif defined(CONFIG_BLE_MESH_ESP32H2_DEV)
// #define LED_R GPIO_NUM_8
// #define LED_G GPIO_NUM_8
// #define LED_B GPIO_NUM_8
// #endif

// #define LED_ON  1
// #define LED_OFF 0

// struct _led_state {
//     uint8_t current;
//     uint8_t previous;
//     uint8_t pin;
//     char *name;
// };

// void board_led_operation(uint8_t pin, uint8_t onoff);

// void board_init(void);

#endif
