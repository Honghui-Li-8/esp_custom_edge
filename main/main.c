#include "board.h"
#include "ble_mesh_config_edge.h"
#include "../Secret/NetworkConfig.h"

#define TAG_M "MAIN"
#define TAG_ALL "*"

static void prov_complete_handler(uint16_t node_index, const esp_ble_mesh_octet16_t uuid, uint16_t addr, uint8_t element_num, uint16_t net_idx) {
    ESP_LOGI(TAG_M, " ----------- prov_complete handler trigered -----------");
}

static void config_complete_handler(uint16_t addr) {
    ESP_LOGI(TAG_M,  " ----------- Node-0x%04x config_complete -----------", addr);
    uart_sendMsg(0,  " ----------- config_complete -----------");
}

static void recv_message_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) {
    // ESP_LOGI(TAG_M, " ----------- recv_message handler trigered -----------");
    ESP_LOGW(TAG_M, "-> Recived Message [%s]", (char*)msg_ptr);

    static uint8_t *data_buffer = NULL;
    if (data_buffer == NULL) {
        data_buffer = (uint8_t*)malloc(128);
        if (data_buffer == NULL) {
            printf("Memory allocation failed.\n");
            return;
        }
    }

    if (strncmp((char*) msg_ptr, "hello world", 11) == 0) {
    
        // "hello world" matched, is pre-setted special message
        // send response to recived remessage
        strcpy((char*)data_buffer, "hello Root, is your code working");
        uint16_t response_length = strlen("hello Root, is your code working") + 1;

        send_response(ctx, response_length, data_buffer);
        ESP_LOGW(TAG_M, "<- Sended Response [%s]", (char*)data_buffer);

        // send message back (initate communication)
        ESP_LOGI(TAG_M, "----------- (send_message) initate a conversation back -----------");
        strcpy((char*)data_buffer, "hello world, this is Edge");
        send_message(ctx->addr, strlen("hello world, this is Edge") + 1, data_buffer);
        ESP_LOGW(TAG_M, "<- Sended Message [%s]", (char*)data_buffer);
        return;
    }
    
    // send response to comfirm recive on other message
    strcpy((char*)data_buffer, "Edge Confirmed recive [");
    strcpy((char*)(data_buffer + 23), (char*) msg_ptr);
    strcpy((char*)(data_buffer + 23 + length), "]");
    uint16_t response_length = strlen((char *)data_buffer) + 1;

    send_response(ctx, response_length, data_buffer);
    ESP_LOGW(TAG_M, "<- Sended Response [%s]", (char*)data_buffer);

}

static void recv_response_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) {
    // ESP_LOGI(TAG_M, " ----------- recv_response handler trigered -----------");
    ESP_LOGW(TAG_M, "-> Recived Response [%s]\n", (char*)msg_ptr);

}

static void timeout_handler(esp_ble_mesh_msg_ctx_t *ctx, uint32_t opcode) {
    ESP_LOGI(TAG_M, " ----------- timeout handler trigered -----------");
    
}

static void execute_uart_command(char* command, size_t cmd_len) {
    size_t cmd_len_raw = cmd_len;

    ESP_LOGI(TAG_M, "execute_command called - %d byte raw - %d decoded byte", cmd_len_raw,  cmd_len);

    static const char *TAG_E = "EXE";
    // static uint8_t *data_buffer = NULL;
    // if (data_buffer == NULL) {
    //     data_buffer = (uint8_t*)malloc(128);
    //     if (data_buffer == NULL) {
    //         printf("Memory allocation failed.\n");
    //         return;
    //     }
    // }

    // ============= process and execute commands from net server (from uart) ==================
    // uart command format
    // TB Finish, TB Complete
    if (cmd_len < 5) {
        ESP_LOGE(TAG_E, "Command [%s] with %d byte too short", command, cmd_len);
        return;
    }
    const size_t CMD_LEN = 5;
    const size_t ADDR_LEN = 2;
    const size_t MSG_SIZE_NUM_LEN = 1;

    if (strncmp(command, "SEND-", 5) == 0) {
        ESP_LOGI(TAG_E, "executing \'SEND-\'");
        char *address_start = command + CMD_LEN;
        char *msg_len_start = address_start + ADDR_LEN;
        char *msg_start = msg_len_start + MSG_SIZE_NUM_LEN;

        uint16_t node_addr = (uint16_t)((address_start[0] << 8) | address_start[1]);
        if (node_addr == 0) {
            node_addr = PROV_OWN_ADDR; // root addr
        }
        size_t msg_length = (size_t)msg_len_start[0];

        // uart_sendData(0, msg_start, msg_length); // sedn back form uart for debugging
        // uart_sendMsg(0, "-feedback \n"); // sedn back form uart for debugging
        // uart_sendMsg(0, node_addr + "--addr-feedback \n"); // sedn back form uart for debugging
        
        ESP_LOGI(TAG_E, "Sending message to address-%d ...", node_addr);
        send_message(node_addr, msg_length, (uint8_t *) msg_start);
        ESP_LOGW(TAG_M, "<- Sended Message \'%.*s\' to node-%d", msg_length, (char*) msg_start, node_addr);
    } else {
        ESP_LOGE(TAG_E, "Command not Vaild");
    }
    
    ESP_LOGI(TAG_E, "Command [%.*s] executed", cmd_len, command);
}

// static void print_bytes(char* data, size_t length) {
//     for(int i = 0; i< length; ++i) {
        
//     }
// }

static void uart_task_handler(char *data) {
    ESP_LOGW(TAG_M, "uart_task_handler called ------------------");

    int cmd_start = 0;
    int cmd_end = 0;
    int cmd_len = 0;

    for (int i = 0; i < UART_BUF_SIZE; i++) {
        if (data[i] == 0xFF) {
            // located start of message
            cmd_start = i + 1; // start byte of actual message
        }

        if (data[i] == 0xFE) {
            // located end of message
            cmd_end = i;  // 0xFE byte
        }

        if (cmd_end > cmd_start) {
            // located a message, message at least 1 byte
            uint8_t* command = (uint8_t *) (data + cmd_start);
            cmd_len = cmd_end - cmd_start;

            // --------- test ----------
            // uart_sendMsg(cmd_len, "[recived]");
            // uart_sendData(0, command, cmd_len); // encoded byte encode and send back
            ESP_LOGE("Encoded Data", "i:%d, cmd_start:%d, cmd_end:%d, cmd_len:%d", i, cmd_start, cmd_end, cmd_len);
            cmd_len = uart_decoded_bytes(command, cmd_len, command); // decoded cmd will be put back to command pointer
            ESP_LOGE("Decoded Data", "i:%d, cmd_start:%d, cmd_len:%d", i, cmd_start, cmd_len);
            
            // uart_sendMsg(cmd_len, "[esp decoded]");
            // uart_sendData(0, command, cmd_len); // decoded byte encode and send back
            // --------- test ----------

            execute_uart_command(data + cmd_start, cmd_len); //TB Finish, don't execute at the moment
        }
    }

    if (cmd_start > cmd_end) {
        // one message is only been read half into buffer, edge case. Not consider at the moment
        ESP_LOGE("E", "Buffer might have remaining half message!! cmd_start:%d, cmd_end:%d", cmd_start, cmd_end);
    }
}

// TB Finish, do we need to make sure there isn't haf of message left in buffer
// when read entire bufffer, no message got read as half, or metho to recover it? 
static void rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "RX";
    uint8_t* data = (uint8_t*) malloc(UART_BUF_SIZE + 1);
    ESP_LOGW(RX_TASK_TAG, "rx_task called ------------------");
    esp_log_level_set(TAG_ALL, ESP_LOG_NONE);

    while (1) {
        const int rxBytes = uart_read_bytes(UART_NUM, data, UART_BUF_SIZE, 1000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            // ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            // uart_sendMsg(rxBytes, " readed from RX\n");

            uart_task_handler((char*) data);
        }
    }
    free(data);
}

void app_main(void)
{
    // turn off log - important, bc the server counting on '[E]' as end of message instaed of '\0'
    //              - since the message from uart carries data
    //              - use uart_sendMsg or uart_sendData for message, the esp_log for dev debug
    esp_log_level_set(TAG_ALL, ESP_LOG_NONE);
    uart_sendMsg(0, "[Ignore_prev][UART] Turning off all Log's from esp_log\n");

    esp_err_t err = esp_module_edge_init(prov_complete_handler, config_complete_handler, recv_message_handler, recv_response_handler, timeout_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_M, "Network Module Initialization failed (err %d)", err);
        return;
    }

    board_init();
    xTaskCreate(rx_task, "uart_rx_task", 1024 * 2, NULL, configMAX_PRIORITIES - 1, NULL);

    ESP_LOGI(TAG_M, " ----------- app_main done -----------");
}