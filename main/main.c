#include "board.h"
#include "time.h"
#include "ble_mesh_config_edge.c"

#define TAG_M "MAIN"

static void prov_complete_handler(uint16_t node_index, const esp_ble_mesh_octet16_t uuid, uint16_t addr, uint8_t element_num, uint16_t net_idx) {
    ESP_LOGI(TAG_M, " ----------- prov_complete handler trigered -----------");
    nodeState = CONNECTED;
    loop_message_connection();
}

static void recv_message_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) {
    // ESP_LOGI(TAG_M, " ----------- recv_message handler trigered -----------");
    ESP_LOGW(TAG_M, "-> Received Message [%s]", (char*)msg_ptr);

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
        // send response to received message 
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
    
    // // send response to comfirm receive on other message
    strcpy((char*)data_buffer, "Edge Confirmed receive [");
    strcpy((char*)(data_buffer + 23), (char*) msg_ptr);
    strcpy((char*)(data_buffer + 23 + length), "]");
    uint16_t response_length = strlen((char *)data_buffer) + 1;

    send_response(ctx, response_length, data_buffer);
    ESP_LOGW(TAG_M, "<- Sended Response [%s]", (char*)data_buffer);

}

static void recv_response_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) {
    // ESP_LOGI(TAG_M, " ----------- recv_response handler trigered -----------");
    ESP_LOGW(TAG_M, "-> Received Response [%s]\n", (char*)msg_ptr);

    // //message went through, reset the timer
    // setTimeout(false);
}

static void timeout_handler(esp_ble_mesh_msg_ctx_t *ctx, uint32_t opcode) {
    ESP_LOGI(TAG_M, " ----------- timeout handler trigered -----------");

    // Print the current value of timeout
    bool currentTimeout = getTimeout();
    ESP_LOGI(TAG_M, " Current timeout value: %s", currentTimeout ? "true" : "false");

    if(!currentTimeout) {
        ESP_LOGI(TAG_M, " Timer is starting to count down ");
        startTimer();
        setTimeout(true);
    }
    else if(getTimeElapsed() > 20.0) // that means timeout already happened once -- and if timeout persist for 20 seconds then reset itself.
    {
        nodeState = DISCONNECTED;
        stop_timer();
        ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
        ESP_ERROR_CHECK(esp_timer_delete(periodic_timer));
        ESP_LOGI(TAG_M, " Resetting the Board "); //i should make a one-hit timer just before resetting.
        board_init();
    }
}

//Create a new handler to handle broadcasting
static void broadcast_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) {
    ESP_LOGI(TAG_M, "Broadcast happened\n");
    
    static uint8_t *data_buffer = NULL;
    if (data_buffer == NULL) {
        data_buffer = (uint8_t*)malloc(128);
        if (data_buffer == NULL) {
            printf("Memory allocation failed.\n");
            return;
        }
    }

    ESP_LOGI(TAG_M, "----------- (send_message) Message back for broadcast -----------");
    strcpy((char*)data_buffer, "Message back for broadcast");
    send_message(ctx->addr, strlen("message back for broadcast") + 1, data_buffer);
    ESP_LOGW(TAG_M, "<- Sended Message [%s]", (char*)data_buffer);
    return;
}

static void connectivity_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) {
    ESP_LOGI(TAG_M, "Checking connectivity\n");

    static uint8_t *data_buffer = NULL;
    if (data_buffer == NULL) {
        data_buffer = (uint8_t*)malloc(128);
        if (data_buffer == NULL) {
            printf("Memory allocation failed.\n");
            return;
        }
    }
    
    ESP_LOGI(TAG_M, "----------- (send_message) iniate message for checking purposes -----------");
    strcpy((char*)data_buffer, "Are we still connected, from edge");
    send_message(ctx->addr, strlen("Are we still connected, from edge") + 1, data_buffer);
    ESP_LOGW(TAG_M, "<- Sended Message [%s]", (char*)data_buffer);

    return;
}

void app_main(void)
{
    esp_err_t err;
    board_init();

    err = esp_module_edge_init(prov_complete_handler, recv_message_handler, recv_response_handler, timeout_handler, broadcast_handler, connectivity_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_ROOT, "Network Module Initialization failed (err %d)", err);
        return;
    }
    
    ESP_LOGI(TAG_M, " ----------- app_main done -----------");
}