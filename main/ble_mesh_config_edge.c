/* main.c - Application main entry point */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_timer.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"

#include "ble_mesh_fast_prov_operation.h"
#include "ble_mesh_fast_prov_client_model.h"
#include "ble_mesh_fast_prov_server_model.h"
#include "ble_mesh_example_init.h"

#include "../Secret/NetworkConfig.h"
#include "board.h"


#define TAG TAG_EDGE
#define TAG_W "Debug"
#define TAG_INFO "Net_Info"

extern struct k_delayed_work send_self_prov_node_addr_timer;
extern bt_mesh_atomic_t fast_prov_cli_flags;

static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = INIT_UUID_MATCH;
static uint8_t prov_start_num = 0;
static bool prov_start = false;

static struct esp_ble_mesh_key {
    uint16_t net_idx;
    uint16_t app_idx;
    uint8_t  app_key[ESP_BLE_MESH_OCTET16_LEN];
} ble_mesh_key;

// static nvs_handle_t NVS_HANDLE;
// static const char * NVS_KEY = NVS_KEY_ROOT;

#define MSG_ROLE MSG_ROLE_EDGE

// static esp_ble_mesh_prov_t provision = {
//     .uuid = dev_uuid,
// };

/* Configuration Client Model user_data */
esp_ble_mesh_client_t config_client;

static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
    .default_ttl = 7,
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_CLI(&config_client),
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
};

static const esp_ble_mesh_client_op_pair_t client_op_pair[] = {
    { ECS_193_MODEL_OP_MESSAGE, ECS_193_MODEL_OP_RESPONSE },
    { ECS_193_MODEL_OP_BROADCAST, ECS_193_MODEL_OP_EMPTY },
};

static const esp_ble_mesh_client_op_pair_t fast_prov_cli_op_pair[] = {
    { ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_INFO_SET,         ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_INFO_STATUS         },
    { ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NET_KEY_ADD,      ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NET_KEY_STATUS      },
    { ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR,        ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_ACK       },
    { ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_GET,    ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_STATUS    },
};

/* Fast Prov Edge Model user_data */
example_fast_prov_server_t fast_prov_edge = {
    .primary_role  = false,
    .max_node_num  = 6,
    .prov_node_cnt = 0x0,
    .unicast_min   = ESP_BLE_MESH_ADDR_UNASSIGNED,
    .unicast_max   = ESP_BLE_MESH_ADDR_UNASSIGNED,
    .unicast_cur   = ESP_BLE_MESH_ADDR_UNASSIGNED,
    .unicast_step  = 0x0,
    .flags         = 0x0,
    .iv_index      = 0x0,
    .net_idx       = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx       = ESP_BLE_MESH_KEY_UNUSED,
    .prim_prov_addr = ESP_BLE_MESH_ADDR_UNASSIGNED,
    .match_len     = 0x0,
    .pend_act      = FAST_PROV_ACT_NONE,
    .state         = STATE_IDLE,
};

static esp_ble_mesh_client_t ecs_193_client = {
    .op_pair_size = ARRAY_SIZE(client_op_pair),
    .op_pair = client_op_pair,
};

/* Fast Prov Root Model user_data */
esp_ble_mesh_client_t fast_prov_root = {
    .op_pair_size = ARRAY_SIZE(fast_prov_cli_op_pair),
    .op_pair = fast_prov_cli_op_pair,
};

static esp_ble_mesh_model_op_t client_op[] = { // operation client will "RECEIVED"
    ESP_BLE_MESH_MODEL_OP(ECS_193_MODEL_OP_RESPONSE, 2),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_op_t server_op[] = { // operation server will "RECEIVED"
    ESP_BLE_MESH_MODEL_OP(ECS_193_MODEL_OP_MESSAGE, 2),
    ESP_BLE_MESH_MODEL_OP(ECS_193_MODEL_OP_BROADCAST, 2),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_op_t fast_prov_srv_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_INFO_SET,             3),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NET_KEY_ADD,         16),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR,            2),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_GET,        0),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_op_t fast_prov_cli_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_INFO_STATUS,          1),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NET_KEY_STATUS,       2),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_ACK,        0),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(ECS_193_CID, ECS_193_MODEL_ID_FP_CLIENT, fast_prov_cli_op, NULL, &fast_prov_root),
    ESP_BLE_MESH_VENDOR_MODEL(ECS_193_CID, ECS_193_MODEL_ID_FP_SERVER, fast_prov_srv_op, NULL, &fast_prov_edge),
    ESP_BLE_MESH_VENDOR_MODEL(ECS_193_CID, ECS_193_MODEL_ID_CLIENT, client_op, NULL, &ecs_193_client), 
    ESP_BLE_MESH_VENDOR_MODEL(ECS_193_CID, ECS_193_MODEL_ID_SERVER, server_op, NULL, NULL),
};

static esp_ble_mesh_model_t *client_model = &vnd_models[2];
static esp_ble_mesh_model_t *server_model = &vnd_models[3];

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = { // composition of current module
    .cid = ECS_193_CID,
    .elements = elements,
    .element_count = ARRAY_SIZE(elements),
};

static esp_ble_mesh_prov_t prov = { 
    .uuid                = dev_uuid,
    .output_size         = 0,
    .output_actions      = 0,
    .prov_attention      = 0x00,
    .prov_algorithm      = 0x00,
    .prov_pub_key_oob    = 0x00,
    .prov_static_oob_val = NULL,
    .prov_static_oob_len = 0x00,
    .flags               = 0x00,
    .iv_index            = 0x00,
};



// -------------------- application level callback functions ------------------
static void (*prov_complete_handler_cb)(uint16_t node_index, const esp_ble_mesh_octet16_t uuid, uint16_t addr, uint8_t element_num, uint16_t net_idx) = NULL;
static void (*recv_message_handler_cb)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) = NULL;
static void (*recv_response_handler_cb)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) = NULL;
static void (*timeout_handler_cb)(esp_ble_mesh_msg_ctx_t *ctx, uint32_t opcode) = NULL;
static void (*broadcast_handler_cb)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) = NULL;


//-------------------- EDGE Network Functions ----------------
static esp_err_t prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    fast_prov_edge.net_idx = net_idx;
    ESP_LOGI(TAG, "net_idx 0x%03x, addr 0x%04x", net_idx, addr);
    ESP_LOGI(TAG, "flags 0x%02x, iv_index 0x%08" PRIx32, flags, iv_index);

    // application level callback, let main() know provision is completed
    prov_complete_handler_cb(0, dev_uuid, addr, 0, net_idx);

    return ESP_OK;
}

static void receive_unprovision_packet(uint8_t dev_uuid[16], uint8_t addr[BLE_MESH_ADDR_LEN],
                                        esp_ble_mesh_addr_type_t addr_type, uint16_t oob_info,
                                        uint8_t adv_type, esp_ble_mesh_prov_bearer_t bearer)
{
    esp_ble_mesh_unprov_dev_add_t add_dev = {0};
    esp_ble_mesh_dev_add_flag_t flag;
    esp_err_t err;

    /* In Fast Provisioning, the Provisioner should only use PB-ADV to provision devices. */
    if (prov_start && (bearer & ESP_BLE_MESH_PROV_ADV)) {
        /* Checks if the device is a reprovisioned one. */
        if (example_is_node_exist(dev_uuid) == false) {
            if ((prov_start_num >= fast_prov_edge.max_node_num) ||
                    (fast_prov_edge.prov_node_cnt >= fast_prov_edge.max_node_num)) {
                return;
            }
        }

        add_dev.addr_type = (uint8_t)addr_type;
        add_dev.oob_info = oob_info;
        add_dev.bearer = (uint8_t)bearer;
        memcpy(add_dev.uuid, dev_uuid, 16);
        memcpy(add_dev.addr, addr, BLE_MESH_ADDR_LEN);
        flag = ADD_DEV_RM_AFTER_PROV_FLAG | ADD_DEV_START_PROV_NOW_FLAG | ADD_DEV_FLUSHABLE_DEV_FLAG;
        err = esp_ble_mesh_provisioner_add_unprov_dev(&add_dev, flag);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: Failed to start provisioning device", __func__);
            return;
        }

        /* If adding unprovisioned device successfully, increase prov_start_num */
        prov_start_num++;
    }

    return;
}

static void provisioner_prov_complete(int node_idx, const uint8_t uuid[16], uint16_t unicast_addr,
                                      uint8_t element_num, uint16_t net_idx)
{
    example_node_info_t *node = NULL;
    esp_err_t err;

    if (example_is_node_exist(uuid) == false) {
        fast_prov_edge.prov_node_cnt++;
    }

    ESP_LOG_BUFFER_HEX("Device uuid", uuid + 2, 6);
    ESP_LOGI(TAG, "Primary address 0x%04x", unicast_addr);

    /* Sets node info */
    err = example_store_node_info(uuid, unicast_addr, element_num, net_idx,
                                  fast_prov_edge.app_idx, LED_OFF);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: Failed to set node info", __func__);
        return;
    }

    /* Gets node info */
    node = example_get_node_info(unicast_addr);
    if (!node) {
        ESP_LOGE(TAG, "%s: Failed to get node info", __func__);
        return;
    }

    if (fast_prov_edge.primary_role == true) {
        /* If the Provisioner is the primary one (i.e. provisioned by the phone), it shall
         * store self-provisioned node addresses;
         * If the node_addr_cnt configured by the phone is small than or equal to the
         * maximum number of nodes it can provision, it shall reset the timer which is used
         * to send all node addresses to the phone.
         */
        err = example_store_remote_node_address(unicast_addr);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: Failed to store node address 0x%04x", __func__, unicast_addr);
            return;
        }
        if (fast_prov_edge.node_addr_cnt != FAST_PROV_NODE_COUNT_MIN &&
            fast_prov_edge.node_addr_cnt <= fast_prov_edge.max_node_num) {
#pragma GCC diagnostic push
#if     __GNUC__ >= 9
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif
            if (bt_mesh_atomic_test_and_clear_bit(fast_prov_edge.srv_flags, GATT_PROXY_ENABLE_START)) {
                k_delayed_work_cancel(&fast_prov_edge.gatt_proxy_enable_timer);
            }
            if (!bt_mesh_atomic_test_and_set_bit(fast_prov_edge.srv_flags, GATT_PROXY_ENABLE_START)) {
                k_delayed_work_submit(&fast_prov_edge.gatt_proxy_enable_timer, GATT_PROXY_ENABLE_TIMEOUT);
            }
#pragma GCC diagnostic pop
        }
    } else {
        /* When a device is provisioned, the non-primary Provisioner shall reset the timer
         * which is used to send node addresses to the primary Provisioner.
         */
        if (bt_mesh_atomic_test_and_clear_bit(&fast_prov_cli_flags, SEND_SELF_PROV_NODE_ADDR_START)) {
            k_delayed_work_cancel(&send_self_prov_node_addr_timer);
        }
        if (!bt_mesh_atomic_test_and_set_bit(&fast_prov_cli_flags, SEND_SELF_PROV_NODE_ADDR_START)) {
            k_delayed_work_submit(&send_self_prov_node_addr_timer, SEND_SELF_PROV_NODE_ADDR_TIMEOUT);
        }
    }

#pragma GCC diagnostic push
#if     __GNUC__ >= 9
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif
    if (bt_mesh_atomic_test_bit(fast_prov_edge.srv_flags, DISABLE_FAST_PROV_START)) {
        /* When a device is provisioned, and the stop_prov flag of the Provisioner has been
         * set, the Provisioner shall reset the timer which is used to stop the provisioner
         * functionality.
         */
        k_delayed_work_cancel(&fast_prov_edge.disable_fast_prov_timer);
        k_delayed_work_submit(&fast_prov_edge.disable_fast_prov_timer, DISABLE_FAST_PROV_TIMEOUT);
    }
#pragma GCC diagnostic pop

    /* The Provisioner will send Config AppKey Add to the node. */
    example_msg_common_info_t info = {
        .net_idx = node->net_idx,
        .app_idx = node->app_idx,
        .dst = node->unicast_addr,
        .timeout = 0,
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 2, 0)
        .role = ROLE_FAST_PROV,
#endif
    };
    err = example_send_config_appkey_add(config_client.model, &info, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: Failed to send Config AppKey Add message", __func__);
        return;
    }
}

static void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
    esp_err_t err;

    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT, err_code %d", param->node_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT, bearer %s",
                    param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT, bearer %s",
                    param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT");
        prov_complete(param->node_prov_complete.net_idx, param->node_prov_complete.addr,
                    param->node_prov_complete.flags, param->node_prov_complete.iv_index);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_RESET_EVT");
        break;
    case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code %d", param->node_set_unprov_dev_name_comp.err_code);
        break;
    //Fast Provisioning Events
    case ESP_BLE_MESH_NODE_PROXY_GATT_DISABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROXY_GATT_DISABLE_COMP_EVT");
        if (fast_prov_edge.primary_role == true) {
            config_server.relay = ESP_BLE_MESH_RELAY_DISABLED;
        }
        prov_start = true;
        break;
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT:
        receive_unprovision_packet(param->provisioner_recv_unprov_adv_pkt.dev_uuid, param->provisioner_recv_unprov_adv_pkt.addr,
                                    param->provisioner_recv_unprov_adv_pkt.addr_type, param->provisioner_recv_unprov_adv_pkt.oob_info,
                                    param->provisioner_recv_unprov_adv_pkt.adv_type, param->provisioner_recv_unprov_adv_pkt.bearer);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT:
        // ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT");
        // ESP_LOGI(TAG, "%s: bearer %s", param->provisioner_prov_link_open.bearer, 
        //                                 param->provisioner_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT:
        // ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT");
        // ESP_LOGI(TAG, "%s: bearer %s, reason 0x%02x", param->provisioner_prov_link_close.bearer,
        //             param->provisioner_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT", param->provisioner_prov_link_close.reason);
        if (prov_start_num) {
            prov_start_num--;
        }
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT");
        provisioner_prov_complete(param->provisioner_prov_complete.node_idx,
                                    param->provisioner_prov_complete.device_uuid,
                                    param->provisioner_prov_complete.unicast_addr,
                                    param->provisioner_prov_complete.element_num,
                                    param->provisioner_prov_complete.netkey_idx);
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT, err_code: %d",
                    param->provisioner_add_unprov_dev_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT, err_code: %d",
                param->provisioner_set_dev_uuid_match_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT, err_code: %d",
                param->provisioner_set_node_name_comp.err_code);
        break;
    case ESP_BLE_MESH_SET_FAST_PROV_INFO_COMP_EVT: {
        ESP_LOGI(TAG, "ESP_BLE_MESH_SET_FAST_PROV_INFO_COMP_EVT");
        ESP_LOGI(TAG, "status_unicast: 0x%02x, status_net_idx: 0x%02x, status_match 0x%02x",
                param->set_fast_prov_info_comp.status_unicast,
                param->set_fast_prov_info_comp.status_net_idx,
                param->set_fast_prov_info_comp.status_match);
        err = example_handle_fast_prov_info_set_comp_evt(fast_prov_edge.model,
                param->set_fast_prov_info_comp.status_unicast,
                param->set_fast_prov_info_comp.status_net_idx,
                param->set_fast_prov_info_comp.status_match);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: Failed to handle Fast Prov Info Set complete event", __func__);
            return;
        }
        break;
    }
    case ESP_BLE_MESH_SET_FAST_PROV_ACTION_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_SET_FAST_PROV_ACTION_COMP_EVT, status_action 0x%02x",
                param->set_fast_prov_action_comp.status_action);
        err = example_handle_fast_prov_action_set_comp_evt(fast_prov_edge.model,
                param->set_fast_prov_action_comp.status_action);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: Failed to handle Fast Prov Action Set complete event", __func__);
            return;
        }
        break;
    default:
        break;
    }
}

// static void print_error(int error_code) {
//     static uint8_t *data_buffer = NULL;
//     if (data_buffer == NULL) {
//         data_buffer = (uint8_t*)malloc(128);
//         if (data_buffer == NULL) {
//             printf("Memory allocation failed.\n");
//             return;
//         }
//     }

//     const char *error_message = esp_err_to_name_r(error_code, (char*) data_buffer, 128);
    
//     ESP_LOGE(TAG, "Error Message [%s]\n", error_message);
// }

static esp_err_t custom_model_bind_appkey(uint16_t app_idx) {
    const esp_ble_mesh_comp_t *comp = NULL;
    esp_ble_mesh_elem_t *element = NULL;
    esp_ble_mesh_model_t *model = NULL;
    int i, j, k;

    comp = &composition;
    if (!comp) {
        return ESP_FAIL;
    }

    for (i = 0; i < comp->element_count; i++) {
        element = &comp->elements[i];
        /* Bind app_idx with SIG models except the Config Client & Server models */
        for (j = 0; j < element->sig_model_count; j++) {
            model = &element->sig_models[j];
            if (model->model_id == ESP_BLE_MESH_MODEL_ID_CONFIG_SRV ||
                    model->model_id == ESP_BLE_MESH_MODEL_ID_CONFIG_CLI) {
                continue;
            }
            for (k = 0; k < ARRAY_SIZE(model->keys); k++) {
                if (model->keys[k] == app_idx) {
                    break;
                }
            }
            if (k != ARRAY_SIZE(model->keys)) {
                continue;
            }
            for (k = 0; k < ARRAY_SIZE(model->keys); k++) {
                if (model->keys[k] == ESP_BLE_MESH_KEY_UNUSED) {
                    model->keys[k] = app_idx;
                    break;
                }
            }
            if (k == ARRAY_SIZE(model->keys)) {
                ESP_LOGE(TAG, "%s: SIG model (model_id 0x%04x) is full of AppKey",
                         __func__, model->model_id);
            }
        }
        /* Bind app_idx with Vendor models */
        for (j = 0; j < element->vnd_model_count; j++) {
            model = &element->vnd_models[j];
            for (k = 0; k < ARRAY_SIZE(model->keys); k++) {
                if (model->keys[k] == app_idx) {
                    break;
                }
            }
            if (k != ARRAY_SIZE(model->keys)) {
                continue;
            }
            for (k = 0; k < ARRAY_SIZE(model->keys); k++) {
                if (model->keys[k] == ESP_BLE_MESH_KEY_UNUSED) {
                    model->keys[k] = app_idx;
                    break;
                }
            }
            if (k == ARRAY_SIZE(model->keys)) {
                ESP_LOGE(TAG, "%s: Vendor model (model_id 0x%04x, cid: 0x%04x) is full of AppKey",
                         __func__, model->vnd.model_id, model->vnd.company_id);
            }
        }
    }

    return ESP_OK;
    // return example_set_app_idx_to_user_data(app_idx);
}

static void ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event, esp_ble_mesh_model_cb_param_t *param)
{
    // static int64_t start_time;
    esp_err_t err;

    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        if (param->model_operation.opcode == ECS_193_MODEL_OP_MESSAGE) {
            recv_message_handler_cb(param->model_operation.ctx, param->model_operation.length, param->model_operation.msg);
        } else if (param->model_operation.opcode == ECS_193_MODEL_OP_RESPONSE) {
            recv_response_handler_cb(param->model_operation.ctx, param->model_operation.length, param->model_operation.msg);
        } else if (param->model_operation.opcode == ECS_193_MODEL_OP_BROADCAST) {
            broadcast_handler_cb(param->model_operation.ctx, param->model_operation.length, param->model_operation.msg);
        } else if (param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_INFO_SET || 
                    param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NET_KEY_ADD ||
                    param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR || 
                    param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_GET) {
            ESP_LOGI(TAG, "%s: Fast prov server receives msg, opcode 0x%04" PRIx32, __func__, param->model_operation.opcode);
            struct net_buf_simple buf = {
                .len = param->model_operation.length,
                .data = param->model_operation.msg,
            };
            //TODO: Read this function call
            err = example_fast_prov_server_recv_msg(param->model_operation.model,
                                                    param->model_operation.ctx, &buf);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to handle fast prov client message", __func__);
                return;
            }
        } else if (param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_INFO_STATUS ||
                    param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NET_KEY_STATUS ||
                    param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_ACK) {
            ESP_LOGI(TAG, "%s: Fast prov client receives msg, opcode 0x%04" PRIx32, __func__, param->model_operation.opcode);
            //TODO: Read this function call
            err = example_fast_prov_client_recv_status(param->model_operation.model,
                    param->model_operation.ctx,
                    param->model_operation.length,
                    param->model_operation.msg);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to handle fast prov server message", __func__);
                return;
            }
            break;
        }
        
        break;
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        if (param->model_send_comp.err_code) {
            ESP_LOGE(TAG, "Failed to send message 0x%06" PRIx32, param->model_send_comp.opcode);
            break;
        }
        if (param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_INFO_SET || 
            param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NET_KEY_ADD ||
            param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR || 
            param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_GET) {
            //TODO: Read this function, opcode is difference
            err = example_handle_fast_prov_status_send_comp_evt(param->model_send_comp.err_code,
                    param->model_send_comp.opcode,
                    param->model_send_comp.model,
                    param->model_send_comp.ctx);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to handle fast prov status send complete event", __func__);
                return;
            }
            break;
        }
        // start_time = esp_timer_get_time();
        ESP_LOGI(TAG, "Send opcode [0x%06" PRIx32 "] completed", param->model_send_comp.opcode);
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
        ESP_LOGI(TAG, "Receive publish message 0x%06" PRIx32, param->client_recv_publish_msg.opcode);
        
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT:
        ESP_LOGW(TAG, "Client message 0x%06" PRIx32 " timeout", param->client_send_timeout.opcode);
        timeout_handler_cb(param->client_send_timeout.ctx, param->client_send_timeout. opcode);
        //TODO: Read function: opcode may be different
        err = example_fast_prov_client_recv_timeout(param->client_send_timeout.opcode,
                param->client_send_timeout.model,
                param->client_send_timeout.ctx);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: Faield to resend fast prov client message", __func__);
            return;
        }
        break;
    default:
        break;
    }
}

void send_message(uint16_t dst_address, uint16_t length, uint8_t *data_ptr)
{
    esp_ble_mesh_msg_ctx_t ctx = {0};
    uint32_t opcode = ECS_193_MODEL_OP_MESSAGE;
    esp_ble_mesh_dev_role_t message_role = MSG_ROLE;
    esp_err_t err = ESP_OK;

    // ESP_LOGW(TAG, "net_idx: %" PRIu16, ble_mesh_key.net_idx);
    // ESP_LOGW(TAG, "app_idx: %" PRIu16, ble_mesh_key.app_idx);
    // ESP_LOGW(TAG, "dst_address: %" PRIu16, dst_address);

    ctx.net_idx = ble_mesh_key.net_idx;
    ctx.app_idx = ble_mesh_key.app_idx;
    ctx.addr = dst_address;
    ctx.send_ttl = MSG_SEND_TTL;
    

    err = esp_ble_mesh_client_model_send_msg(client_model, &ctx, opcode, length, data_ptr, MSG_TIMEOUT, true, message_role);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send message to node addr 0x%04x, err_code %d", dst_address, err);
        return;
    }
}

void send_broadcast(uint16_t length, uint8_t *data_ptr)
{
    esp_ble_mesh_msg_ctx_t ctx = {0};
    uint32_t opcode = ECS_193_MODEL_OP_BROADCAST;
    esp_ble_mesh_dev_role_t message_role = MSG_ROLE;
    esp_err_t err = ESP_OK;

    // ESP_LOGW(TAG, "net_idx: %" PRIu16, ble_mesh_key.net_idx);
    // ESP_LOGW(TAG, "app_idx: %" PRIu16, ble_mesh_key.app_idx);
    // ESP_LOGW(TAG, "dst_address: %" PRIu16, dst_address);

    ctx.net_idx = ble_mesh_key.net_idx;
    ctx.app_idx = ble_mesh_key.app_idx;
    ctx.addr = 0xFFFF;
    ctx.send_ttl = MSG_SEND_TTL;
    
    //false value means doesn't need response (broadcast can't have response)
    err = esp_ble_mesh_client_model_send_msg(client_model, &ctx, opcode, length, data_ptr, MSG_TIMEOUT, false, message_role);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send message to node addr 0xFFFF, err_code %d", err);
        return;
    }
}

void send_response(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *data_ptr)
{
    uint32_t opcode = ECS_193_MODEL_OP_RESPONSE;
    esp_err_t err;

    ESP_LOGW(TAG, "response net_idx: %" PRIu16, ctx->net_idx);
    ESP_LOGW(TAG, "response app_idx: %" PRIu16, ctx->app_idx);
    ESP_LOGW(TAG, "response addr: %" PRIu16, ctx->addr);
    ESP_LOGW(TAG, "response recv_dst: %" PRIu16, ctx->recv_dst);

    err = esp_ble_mesh_server_model_send_msg(server_model, ctx, opcode, length, data_ptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send message to node addr 0x%04x, err_code %d", ctx->addr, err);
        return;
    }
}

// ========================= our function ==================================

static void example_ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                              esp_ble_mesh_cfg_server_cb_param_t *param)
{
    esp_err_t err;

    if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
            ESP_LOGI(TAG, "net_idx 0x%04x, app_idx 0x%04x",
                param->value.state_change.appkey_add.net_idx,
                param->value.state_change.appkey_add.app_idx);
            ESP_LOG_BUFFER_HEX("AppKey", param->value.state_change.appkey_add.app_key, 16);

            err = custom_model_bind_appkey(param->value.state_change.appkey_add.app_idx);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to bind app_idx 0x%04x with non-config models",
                    __func__, param->value.state_change.appkey_add.app_idx);
                return;
            }
            ble_mesh_key.app_idx = param->value.state_change.mod_app_bind.app_idx;
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
            ESP_LOGI(TAG, "elem_addr 0x%04x, app_idx 0x%04x, cid 0x%04x, mod_id 0x%04x",
                param->value.state_change.mod_app_bind.element_addr,
                param->value.state_change.mod_app_bind.app_idx,
                param->value.state_change.mod_app_bind.company_id,
                param->value.state_change.mod_app_bind.model_id);

            ble_mesh_key.app_idx = param->value.state_change.mod_app_bind.app_idx;
            break;
        default:
            break;
        }
    }
}

//TODO: Read over function
static void example_ble_mesh_config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
        esp_ble_mesh_cfg_client_cb_param_t *param) 
{
    example_node_info_t *node = NULL;
    uint32_t opcode;
    uint16_t address;
    esp_err_t err;

    ESP_LOGI(TAG, "%s, error_code = 0x%02x, event = 0x%02x, addr: 0x%04x",
             __func__, param->error_code, event, param->params->ctx.addr);

    opcode = param->params->opcode;
    address = param->params->ctx.addr;

    node = example_get_node_info(address);
    if (!node) {
        ESP_LOGE(TAG, "%s: Failed to get node info", __func__);
        return;
    }

    if (param->error_code) {
        ESP_LOGE(TAG, "Failed to send config client message, opcode: 0x%04" PRIx32, opcode);
        return;
    }

    switch (event) {
    case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT:
        break;
    case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
        switch (opcode) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD: {
            example_fast_prov_info_set_t set = {0};
            if (node->reprov == false) {
                /* After sending Config AppKey Add successfully, start to send Fast Prov Info Set */
                if (fast_prov_edge.unicast_cur >= fast_prov_edge.unicast_max) {
                    /* TODO:
                     * 1. If unicast_cur is >= unicast_max, we can also send the message to enable
                     * the Provisioner functionality on the node, and need to add another vendor
                     * message used by the node to require a new unicast address range from primary
                     * Provisioner, and before get the correct response, the node should pend
                     * the fast provisioning functionality.
                     * 2. Currently if address is not enough, the Provisioner will only add the group
                     * address to the node.
                     */
                    ESP_LOGW(TAG, "%s: Not enough address to be assigned", __func__);
                    node->lack_of_addr = true;
                } else {
                    /* Send fast_prov_info_set message to node */
                    node->lack_of_addr = false;
                    node->unicast_min = fast_prov_edge.unicast_cur;
                    if (fast_prov_edge.unicast_cur + fast_prov_edge.unicast_step >= fast_prov_edge.unicast_max) {
                        node->unicast_max = fast_prov_edge.unicast_max;
                    } else {
                        node->unicast_max = fast_prov_edge.unicast_cur + fast_prov_edge.unicast_step;
                    }
                    node->flags      = fast_prov_edge.flags;
                    node->iv_index   = fast_prov_edge.iv_index;
                    node->fp_net_idx = fast_prov_edge.net_idx;
                    node->group_addr = fast_prov_edge.group_addr;
                    node->prov_addr  = fast_prov_edge.prim_prov_addr;
                    node->match_len  = fast_prov_edge.match_len;
                    memcpy(node->match_val, fast_prov_edge.match_val, fast_prov_edge.match_len);
                    node->action = FAST_PROV_ACT_ENTER;
                    fast_prov_edge.unicast_cur = node->unicast_max + 1;
                }
            }
            if (node->lack_of_addr == false) {
                set.ctx_flags = 0x03FE;
                memcpy(&set.unicast_min, &node->unicast_min,
                       sizeof(example_node_info_t) - offsetof(example_node_info_t, unicast_min));
            } else {
                set.ctx_flags  = BIT(6);
                set.group_addr = fast_prov_edge.group_addr;
            }
            example_msg_common_info_t info = {
                .net_idx = node->net_idx,
                .app_idx = node->app_idx,
                .dst = node->unicast_addr,
                .timeout = 0,
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 2, 0)
                .role = ROLE_FAST_PROV,
#endif
            };
            err = example_send_fast_prov_info_set(fast_prov_root.model, &info, &set);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to send Fast Prov Info Set message", __func__);
                return;
            }
            break;
        }
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_PUBLISH_EVT:
        break;
    case ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT:
        switch (opcode) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD: {
            example_msg_common_info_t info = {
                .net_idx = node->net_idx,
                .app_idx = node->app_idx,
                .dst = node->unicast_addr,
                .timeout = 0,
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 2, 0)
                .role = ROLE_FAST_PROV,
#endif
            };
            err = example_send_config_appkey_add(config_client.model, &info, NULL);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to send Config AppKey Add message", __func__);
                return;
            }
            break;
        }
        default:
            break;
        }
        break;
    default:
        return;
    }
}

static esp_err_t ble_mesh_init(void)
{
    esp_err_t err;
    
    // ble_mesh_key.net_idx = ESP_BLE_MESH_KEY_PRIMARY;
    ble_mesh_key.app_idx = APP_KEY_IDX;

    esp_ble_mesh_register_prov_callback(ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_server_callback(example_ble_mesh_config_server_cb);
    esp_ble_mesh_register_config_client_callback(example_ble_mesh_config_client_cb);
    esp_ble_mesh_register_custom_model_callback(ble_mesh_custom_model_cb);

    err = esp_ble_mesh_init(&prov, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mesh stack");
        return err;
    }

    // err = esp_ble_mesh_client_model_init(&vnd_models[0]);
    // if (err) {
    //     ESP_LOGE(TAG, "Failed to initialize vendor client");
    //     return err;
    // }

    // err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to enable mesh node");
    //     return err;
    // }

    err = example_fast_prov_server_init(&vnd_models[0]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: Failed to initialize fast prov server model", __func__);
        return err;
    }

    err = esp_ble_mesh_client_model_init(&vnd_models[1]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: Failed to initialize fast prov client model", __func__);
        return err;
    }

    k_delayed_work_init(&send_self_prov_node_addr_timer, example_send_self_prov_node_addr);

    err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: Failed to enable node provisioning", __func__);
        return err;
    }

    ESP_LOGI(TAG, "BLE Mesh Fast Prov Node initialized");

    // ESP_LOGI(TAG, "BLE Mesh Node initialized");

    return ESP_OK;
}



static esp_err_t esp_module_edge_init(
    void (*prov_complete_handler)(uint16_t node_index, const esp_ble_mesh_octet16_t uuid, uint16_t addr, uint8_t element_num, uint16_t net_idx),
    void (*recv_message_handler)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr),
    void (*recv_response_handler)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr),
    void (*timeout_handler)(esp_ble_mesh_msg_ctx_t *ctx, uint32_t opcode),
    void (*broadcast_handler)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr)
) {
    esp_err_t err;

    ESP_LOGI(TAG, "Initializing EDGE Module...");

    // attach application level callback
    prov_complete_handler_cb = prov_complete_handler;
    recv_message_handler_cb = recv_message_handler;
    recv_response_handler_cb = recv_response_handler;
    timeout_handler_cb = timeout_handler;
    broadcast_handler_cb = broadcast_handler;
    if (prov_complete_handler_cb == NULL || recv_message_handler_cb == NULL || recv_response_handler_cb == NULL || timeout_handler_cb == NULL || broadcast_handler_cb == NULL) {
        ESP_LOGE(TAG, "Application Level Callback function is NULL");
        return ESP_FAIL;
    }

    
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = bluetooth_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
        return ESP_FAIL;
    }

    ble_mesh_get_dev_uuid(dev_uuid);

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Done Initializing...");
    return ESP_OK;
}