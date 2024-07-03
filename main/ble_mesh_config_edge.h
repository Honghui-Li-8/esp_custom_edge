#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_timer.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"

#include "ble_mesh_example_init.h"
#include "ble_mesh_example_nvs.h"

#include "../Secret/NetworkConfig.h"

#ifndef _BLE_EDGE_H_
#define _BLE_EDGE_H_

/**
 * @brief Loop message connection for handling incoming and outgoing messages.
 */
void loop_message_connection();

/**
 * @brief Stop the ESP timer.
 */
void stop_esp_timer();

/**
 * @brief Stop the periodic timer.
 */
void stop_periodic_timer();

/**
 * @brief Get the current state of the node.
 * 
 * @return Current state of the node.
 */
enum State getNodeState();

/**
 * @brief Set the state of the node.
 * 
 * @param state New state of the node.
 */
void setNodeState(enum State state);

/**
 * @brief Set the Time To Live (TTL) for messages.
 * 
 * @param new_ttl New TTL value.
 */
void set_message_ttl(uint8_t new_ttl);

/**
 * @brief Send a message to a specific address.
 * 
 * @param dst_address Destination address.
 * @param length Length of the message.
 * @param data_ptr Pointer to the message data.
 * @param require_response Flag indicating if a response is required.
 */
void send_message(uint16_t dst_address, uint16_t length, uint8_t *data_ptr, bool require_response);

// vvv important message functions

/**
 * @brief Send an important message to a specific address.
 * 
 * @param dst_address Destination address.
 * @param length Length of the message.
 * @param data_ptr Pointer to the message data.
 */
void send_important_message(uint16_t dst_address, uint16_t length, uint8_t *data_ptr);

/**
 * @brief Get the index of an important message by its opcode.
 * 
 * @param opcode Opcode of the message.
 * @return Index of the important message.
 */
int8_t get_important_message_index(uint32_t opcode);

/**
 * @brief Retransmit an important message.
 * 
 * @param ctx_ptr Pointer to the context of the message.
 * @param opcode Opcode of the message.
 * @param index Index of the important message.
 */
void retransmit_important_message(esp_ble_mesh_msg_ctx_t* ctx_ptr, uint32_t opcode, int8_t index);

/**
 * @brief Clear an important message from storage.
 * 
 * @param index Index of the important message.
 */
void clear_important_message(int8_t index);

// ^^^ important message functions

/**
 * @brief Broadcast a message to all nodes.
 * 
 * @param length Length of the message.
 * @param data_ptr Pointer to the message data.
 */
void broadcast_message(uint16_t length, uint8_t *data_ptr);

/**
 * @brief Send a response to a received message.
 * 
 * @param ctx Context of the received message.
 * @param length Length of the response message.
 * @param data_ptr Pointer to the response message data.
 * @param message_opcode Opcode of the message.
 */
void send_response(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *data_ptr, uint32_t message_opcode);

/**
 * @brief Reset the ESP32 device.
 */
void reset_esp32();

/**
 * @brief Reset the edge device.
 */
void reset_edge();

esp_err_t esp_module_edge_init(
    void (*prov_complete_handler)(uint16_t node_index, const esp_ble_mesh_octet16_t uuid, uint16_t addr, uint8_t element_num, uint16_t net_idx),
    void (*config_complete_handler)(uint16_t addr),
    void (*recv_message_handler)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr, uint32_t opcode),
    void (*recv_response_handler)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr, uint32_t opcode),
    void (*timeout_handler)(esp_ble_mesh_msg_ctx_t *ctx, uint32_t opcode),
    void (*broadcast_handler)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr),
    void (*connectivity_handler)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr)
);

#endif /* _BLE_EDGE_H_ */