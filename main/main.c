#include "board.h"
#include "ble_mesh_config_edge.c"

#define TAG_M "MAIN"

static void prov_complete_handler(uint16_t node_index, const esp_ble_mesh_octet16_t uuid, uint16_t addr, uint8_t element_num, uint16_t net_idx) {
    ESP_LOGI(TAG_M, " ----------- prov_complete handler trigered -----------");
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
}

static void recv_response_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) {
    // ESP_LOGI(TAG_M, " ----------- recv_response handler trigered -----------");
    ESP_LOGW(TAG_M, "-> Recived Response [%s]\n", (char*)msg_ptr);

}

static void timeout_handler(esp_ble_mesh_msg_ctx_t *ctx, uint32_t opcode) {
    ESP_LOGI(TAG_M, " ----------- timeout handler trigered -----------");
    
}


void app_main(void)
{
    esp_err_t err;
    board_init();

#if defined(ROOT_MODULE)
    err = esp_module_root_init(prov_complete_handler, recv_message_handler, recv_response_handler, timeout_handler);
#else
    err = esp_module_edge_init(prov_complete_handler, recv_message_handler, recv_response_handler, timeout_handler);
#endif

    if (err != ESP_OK) {
        ESP_LOGE(TAG_ROOT, "Network Module Initialization failed (err %d)", err);
        return;
    }

    
    ESP_LOGI(TAG_M, " ----------- app_main done -----------");
}