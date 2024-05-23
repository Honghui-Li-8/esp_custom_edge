#ifndef NETCONFIG_H
#define NETCONFIG_H

#define TAG_ROOT "ROOT"
#define TAG_EDGE "EDGE"
//#define ROOT_MODULE // control which module it is

#define ECS_193_CID         0x02E5  // regulate the module connecting
#define APP_KEY_IDX         0x0000
#define APP_KEY_OCTET       0x12

#define PROV_OWN_ADDR       0x0001
#define PROV_START_ADDR     0x0005

#define INIT_UUID_MATCH     { 0x32, 0x10 } // regulate the node get provitioned

#define MSG_SEND_TTL        3
#define MSG_TIMEOUT         0
#define MSG_ROLE_ROOT       ROLE_PROVISIONER
#define MSG_ROLE_EDGE       ROLE_NODE
// #define MSG_ROLE_EDGE       ROLE_NODE // ROLE_FAST_PROV // ROLE_NODE

#define COMP_DATA_PAGE_0    0x00

#define COMP_DATA_1_OCTET(msg, offset)      (msg[offset])
#define COMP_DATA_2_OCTET(msg, offset)      (msg[offset + 1] << 8 | msg[offset])

#define ECS_193_MODEL_ID_CLIENT         0x0000
#define ECS_193_MODEL_ID_SERVER         0x0001
#define ECS_193_MODEL_ID_FP_CLIENT      0x0002
#define ECS_193_MODEL_ID_FP_SERVER      0x0003

#define ECS_193_MODEL_OP_CUSTOM      ESP_BLE_MESH_MODEL_OP_3(0x00, ECS_193_CID)
#define ECS_193_MODEL_OP_MESSAGE     ESP_BLE_MESH_MODEL_OP_3(0x01, ECS_193_CID)
#define ECS_193_MODEL_OP_RESPONSE    ESP_BLE_MESH_MODEL_OP_3(0x02, ECS_193_CID)
#define ECS_193_MODEL_OP_BROADCAST   ESP_BLE_MESH_MODEL_OP_3(0x03, ECS_193_CID)
#define ECS_193_MODEL_OP_EMPTY       ESP_BLE_MESH_MODEL_OP_3(0x04, ECS_193_CID)

// /* Fast Prov Message Opcode */
// #define ECS_193_MODEL_OP_FP_INFO_SET            ESP_BLE_MESH_MODEL_OP_3(0x05, ECS_193_CID)
// #define ECS_193_MODEL_OP_FP_INFO_STATUS         ESP_BLE_MESH_MODEL_OP_3(0x06, ECS_193_CID)

// #define ECS_193_MODEL_OP_FP_NET_KEY_ADD         ESP_BLE_MESH_MODEL_OP_3(0x07, ECS_193_CID)
// #define ECS_193_MODEL_OP_FP_NET_KEY_STATUS      ESP_BLE_MESH_MODEL_OP_3(0x08, ECS_193_CID)

// #define ECS_193_MODEL_OP_FP_NODE_ADDR_GET       ESP_BLE_MESH_MODEL_OP_3(0x09, ECS_193_CID)
// #define ECS_193_MODEL_OP_FP_NODE_ADDR_STATUS    ESP_BLE_MESH_MODEL_OP_3(0x10, ECS_193_CID)

// #define ECS_193_MODEL_OP_FP_ADDR                ESP_BLE_MESH_MODEL_OP_3(0x11, ECS_193_CID)
// #define ECS_193_MODEL_OP_FP_ADDR_ACK            ESP_BLE_MESH_MODEL_OP_3(0x12, ECS_193_CID)


#define NVS_KEY_ROOT "ECS_193_client"

#endif /* NETCONFIG_H */