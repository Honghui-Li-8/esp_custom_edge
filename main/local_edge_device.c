#include <stdio.h>
#include "esp_log.h"
#include "iot_button.h"
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include "esp_timer.h"
#include "../Secret/NetworkConfig.h"

#define MAX_MSG_LEN 256
#define BLE_CMD_LEN 5
#define BLE_ADDR_LEN 2
#define TAG_L "[Local Edge]"

esp_timer_handle_t data_send_timer;

uint64_t data_send_interval = 1000000; // 1 second
bool running_test = false;
bool sending_data = false;

extern void execute_network_command(char *command, size_t cmd_total_len);

void dispatch_network_command(char* ble_cmd, uint16_t node_addr, uint8_t *data_buffer, size_t data_length)
{
    uint8_t command_msg[MAX_MSG_LEN + BLE_CMD_LEN + BLE_ADDR_LEN];
    memset(command_msg, 0, MAX_MSG_LEN + BLE_CMD_LEN + BLE_ADDR_LEN);
    uint8_t *msg_itr = command_msg;
    uint16_t node_addr_network_order = htons(node_addr);

    if (data_length > MAX_MSG_LEN)
    {
        ESP_LOGE(TAG_L, "Local Edge Device Trying to Send %d bytes message that's more than MAX_MSG_LEN-%d", (int) data_length, (int) MAX_MSG_LEN);
        return;
    }

    memcpy(msg_itr, ble_cmd, BLE_CMD_LEN);
    msg_itr += BLE_CMD_LEN;
    memcpy(msg_itr, &node_addr_network_order, BLE_ADDR_LEN);
    msg_itr += BLE_ADDR_LEN;

    if (data_buffer != NULL) {
        memcpy(msg_itr, data_buffer, data_length);
        msg_itr += data_length;
    }

    ESP_LOGI(TAG_L, "data_buffer: '%.*s'", data_length, data_buffer);
    execute_network_command((char *) command_msg, msg_itr - command_msg);
}

void ble_send_to_root(uint8_t *data_buffer, size_t data_length)
{
    char ble_cmd[7] = "SEND-";
    ESP_LOGI(TAG_L, "data_buffer: '%.*s'", data_length, data_buffer);
    dispatch_network_command(ble_cmd, 0, data_buffer, data_length);
}

void sendtMultipleData_Example(int16_t *fake_gps)
{
    // Data update opcode | data_type_byte | data
    // D | 0x00 | sequence_number_1_byte | 0x01 | GPS_6_bytes
    static uint8_t sequence_number = 0;
    uint8_t buffer[MAX_MSG_LEN];
    uint8_t *buf_itr = buffer;

    // message type / opcode
    strncpy((char *)buf_itr, "D", 1);
    buf_itr += 1;

    // 1 byte size_n of data amount
    buf_itr[0] = 0x02; // has 2 data
    buf_itr += 1;

    // data type
    buf_itr[0] = 0x00; // sequence number
    buf_itr += 1;
    // data
    buf_itr[0] = sequence_number;
    buf_itr += 1;

    // ------------ data ----------------
    // data type
    buf_itr[0] = 0x01; // sequence number
    buf_itr += 1;
    
    // fake temp GPS Data - 6 byte
    memcpy(buf_itr, fake_gps, 2);
    buf_itr += 2;
    memcpy(buf_itr, fake_gps, 2);
    buf_itr += 2;
    memcpy(buf_itr, fake_gps, 2);
    buf_itr += 2;

    ble_send_to_root(buffer, buf_itr - buffer);
    sequence_number += 1;
}

void sendData() {
    static int16_t fake_gps = 333;

    sendtMultipleData_Example(&fake_gps);

    fake_gps += 3;
}

void sendRobotRequest()
{
    static int count = 0;
    ESP_LOGI(TAG_L, "Sending Robot Request: %d", count++);

    uint8_t buffer[10];
    uint8_t *buf_itr = buffer;

    // message type
    strncpy((char *)buf_itr, "R", 1);
    buf_itr += 1;

    buf_itr[0] = 0x00; // robot request
    buf_itr += 1;

    ble_send_to_root(buffer, buf_itr - buffer);
}

void create_data_send_event()
{
    if (sending_data == true) {
        return;
    }

    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &sendData,
        // .callback = &periodic_timer_callback,
        /* name is optional, but may help identify the timer when debugging */
        .name = "data_send"};

    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &data_send_timer));

    ESP_ERROR_CHECK(esp_timer_start_periodic(data_send_timer, data_send_interval));
    sending_data = true;
}

void stop_data_send_event() {
    if (sending_data == false) {
        return;
    }
    ESP_ERROR_CHECK(esp_timer_stop(data_send_timer));
    sending_data = false;
}

void start_current_test(char* current_test) {
    ESP_LOGI(TAG_L, "IS 'T|S' test start");
    
    if (strncmp(current_test, "0", 1) == 0)
    {
        // restart module
        char ble_cmd[7] = "RST-E";
        dispatch_network_command(ble_cmd, 0, NULL, 0);
    }
    else if (strncmp(current_test, "D", 1) == 0)
    {
        create_data_send_event();
    }
    else if (strncmp(current_test, "R", 1) == 0)
    {
        // create_robot_request_event();
    }

    running_test = true;
}

void stop_current_test(char* current_test)
{
    if (!running_test) {
        return;
    }

    if (strncmp(current_test, "D", 5) == 0)
    {
        stop_data_send_event();
    }

    memcpy(current_test, "---", 1);
    running_test = false;
}

void local_edge_device_network_message_handler(uint16_t node_addr, uint8_t *data, size_t length) {
    static char current_test[10] = "";
    char *opcode = (char *)data;
    char *payload = (char *)data + OPCODE_LEN;

    ESP_LOGI(TAG_L, "recived: %d bytes, opcode: '%.*s', payload: \'%.*s\', from node-%d", length, OPCODE_LEN, opcode, length - OPCODE_LEN, payload, node_addr);

    if (strncmp(opcode, "T", OPCODE_LEN) == 0)
    {
        // is our Test opcode 'T'
        if (strncmp(payload, "I", 1) == 0)
        {
            char *test_name = payload + 1;
            if (running_test)
            {
                ESP_LOGI(TAG_L, "some test is running");
                // return; // running other test
            }

            ESP_LOGI(TAG_L, "IS 'T|I' test initialization");
            memcpy(current_test, test_name, 1);

            // Initialization of test ..................................
            // Confirm ready for test ..................................

            uint8_t buffer[MAX_MSG_LEN];
            uint8_t *buf_itr = buffer;

            // message type
            strncpy((char *)buf_itr, "A", OPCODE_LEN);
            buf_itr += OPCODE_LEN;
            
            ESP_LOGI(TAG_L, "send out: '%.*s'", buf_itr - buffer, buffer);
            ble_send_to_root(buffer, buf_itr - buffer);
        }
        else if (strncmp(payload, "S", 1) == 0)
        {
            start_current_test(current_test);
        }
        else if (strncmp(payload, "F", 1) == 0)
        {
            stop_current_test(current_test);
        }
    }
    else if ((strncmp(opcode, "S", OPCODE_LEN) == 0))
    {
        ESP_LOGW(TAG_L, "Resetting");
        char ble_cmd[7] = "RST-E";
        dispatch_network_command(ble_cmd, 0, NULL, 0);
    }
    else if (strncmp(opcode, "E", OPCODE_LEN) == 0)
    {
        uint8_t buffer[MAX_MSG_LEN];
        uint8_t *buf_itr = buffer;

        // message type
        strncpy((char *)buf_itr, "A", OPCODE_LEN);
        buf_itr += OPCODE_LEN;
        memcpy(buf_itr, payload, length - OPCODE_LEN);
        buf_itr += length - OPCODE_LEN;
        ESP_LOGI(TAG_L, "send out: '%.*s'", buf_itr - buffer, buffer);
        ble_send_to_root(buffer, buf_itr - buffer);
    }
}

// void local_edge_device_task()
// {
// }

void local_edge_device_init() {
    // any logic need to be on its thread for local edge device to run
    // xTaskCreate(local_edge_device_task, "local_edge_device_task", 1024 * 2, NULL, configMAX_PRIORITIES - 2, NULL);
}