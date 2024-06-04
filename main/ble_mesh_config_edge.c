/* main.c - Application main entry point */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "board.h"
#include "ble_mesh_config_edge.h"
#include "../Secret/NetworkConfig.h"

#include "esp_ble_mesh_local_data_operation_api.h"
#if CONFIG_BLE_MESH_RPR_SRV
#include "esp_ble_mesh_rpr_model_api.h"
#endif

#define TAG TAG_EDGE
#define TAG_W "Debug"
#define TAG_INFO "Net_Info"
#define timer_for_ping 6000000 //6 seconds for pinging root to check conectivity

enum State nodeState = DISCONNECTED;
esp_timer_handle_t periodic_timer;

static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = INIT_UUID_MATCH;
static struct esp_ble_mesh_key {
    uint16_t net_idx;
    uint16_t app_idx;
    uint8_t  app_key[ESP_BLE_MESH_OCTET16_LEN];
} ble_mesh_key;

// static nvs_handle_t NVS_HANDLE;
// static const char * NVS_KEY = NVS_KEY_ROOT;


#define MSG_ROLE MSG_ROLE_EDGE

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
#if 0
    .output_size = 4,
    .output_actions = ESP_BLE_MESH_DISPLAY_NUMBER,
    .input_actions = ESP_BLE_MESH_PUSH,
    .input_size = 4,
#else
    .output_size = 0,
    .output_actions = 0,
#endif
};

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
#if CONFIG_BLE_MESH_RPR_SRV
    ESP_BLE_MESH_MODEL_RPR_SRV(NULL),
#endif
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
};

static const esp_ble_mesh_client_op_pair_t client_op_pair[] = {
    { ECS_193_MODEL_OP_MESSAGE, ECS_193_MODEL_OP_RESPONSE },
    { ECS_193_MODEL_OP_BROADCAST, NULL },
    { ECS_193_MODEL_OP_CONNECTIVITY, ECS_193_MODEL_OP_RESPONSE},
};

static esp_ble_mesh_client_t ecs_193_client = {
    .op_pair_size = ARRAY_SIZE(client_op_pair),
    .op_pair = client_op_pair,
};

static esp_ble_mesh_model_op_t client_op[] = { // operation client will "RECEIVED"
    ESP_BLE_MESH_MODEL_OP(ECS_193_MODEL_OP_RESPONSE, 1),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_op_t server_op[] = { // operation server will "RECEIVED"
    ESP_BLE_MESH_MODEL_OP(ECS_193_MODEL_OP_MESSAGE, 2),
    ESP_BLE_MESH_MODEL_OP(ECS_193_MODEL_OP_BROADCAST, 2),
    ESP_BLE_MESH_MODEL_OP(ECS_193_MODEL_OP_CONNECTIVITY, 1),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t vnd_models[] = { // custom models
    ESP_BLE_MESH_VENDOR_MODEL(ECS_193_CID, ECS_193_MODEL_ID_CLIENT, client_op, NULL, &ecs_193_client), 
    ESP_BLE_MESH_VENDOR_MODEL(ECS_193_CID, ECS_193_MODEL_ID_SERVER, server_op, NULL, NULL),
};

static esp_ble_mesh_model_t *client_model = &vnd_models[0];
static esp_ble_mesh_model_t *server_model = &vnd_models[1];

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = { // composition of current module
    .cid = ECS_193_CID,
    .elements = elements,
    .element_count = ARRAY_SIZE(elements),
};



// -------------------- application level callback functions ------------------
static void (*prov_complete_handler_cb)(uint16_t node_index, const esp_ble_mesh_octet16_t uuid, uint16_t addr, uint8_t element_num, uint16_t net_idx) = NULL;
static void (*config_complete_handler_cb)(uint16_t addr) = NULL;
static void (*recv_message_handler_cb)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) = NULL;
static void (*recv_response_handler_cb)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) = NULL;
static void (*timeout_handler_cb)(esp_ble_mesh_msg_ctx_t *ctx, uint32_t opcode) = NULL;
static void (*broadcast_handler_cb)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) = NULL;
static void (*connectivity_handler_cb)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) = NULL;;


//-------------------- EDGE Network Functions ----------------
static esp_err_t prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    ble_mesh_key.net_idx = net_idx;
    ESP_LOGI(TAG, "net_idx 0x%03x, addr 0x%04x", net_idx, addr);
    ESP_LOGI(TAG, "flags 0x%02x, iv_index 0x%08" PRIx32, flags, iv_index);

    // application level callback, let main() know provision is completed
    prov_complete_handler_cb(0, dev_uuid, addr, 0, net_idx);

    return ESP_OK;
}

static void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT, err_code %d", param->node_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        nodeState = CONNECTING;
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
    default:
        break;
    }
}

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

    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        if (param->model_operation.opcode == ECS_193_MODEL_OP_MESSAGE) {
            recv_message_handler_cb(param->model_operation.ctx, param->model_operation.length, param->model_operation.msg);
        } else if (param->model_operation.opcode == ECS_193_MODEL_OP_RESPONSE) {
            recv_response_handler_cb(param->model_operation.ctx, param->model_operation.length, param->model_operation.msg);
        } else if (param->model_operation.opcode == ECS_193_MODEL_OP_BROADCAST) {
            broadcast_handler_cb(param->model_operation.ctx, param->model_operation.length, param->model_operation.msg);
        } else if (param->model_operation.opcode == ECS_193_MODEL_OP_CONNECTIVITY) {
            connectivity_handler_cb(param->model_operation.ctx, param->model_operation.length, param->model_operation.msg);
        }
        
        break;
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        if (param->model_send_comp.err_code) {
            ESP_LOGE(TAG, "Failed to send message 0x%06" PRIx32, param->model_send_comp.opcode);
            break;
        }
        // start_time = esp_timer_get_time();
        ESP_LOGI(TAG, "Send opcode [0x%06" PRIx32 "] completed", param->model_send_comp.opcode);
        setNodeState(CONNECTED);
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
        ESP_LOGI(TAG, "Receive publish message 0x%06" PRIx32, param->client_recv_publish_msg.opcode);
        
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT:
        ESP_LOGW(TAG, "Client message 0x%06" PRIx32 " timeout", param->client_send_timeout.opcode);
        timeout_handler_cb(param->client_send_timeout.ctx, param->client_send_timeout.opcode);
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
    
    setNodeState(WORKING);
    err = esp_ble_mesh_client_model_send_msg(client_model, &ctx, opcode, length, data_ptr, MSG_TIMEOUT, true, message_role);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send message to node addr 0x%04x, err_code %d", dst_address, err);
        return;
    }
    
}

void broadcast_message(uint16_t length, uint8_t *data_ptr)
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
    

    err = esp_ble_mesh_client_model_send_msg(client_model, &ctx, opcode, length, data_ptr, MSG_TIMEOUT, false, message_role);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send message to node addr 0xFFFF, err_code %d", err);
        return;
    }
}

void send_connectivity(uint16_t dst_address, uint16_t length, uint8_t *data_ptr)
{
    esp_ble_mesh_msg_ctx_t ctx = {0};
    uint32_t opcode = ECS_193_MODEL_OP_CONNECTIVITY;
    esp_ble_mesh_dev_role_t message_role = MSG_ROLE;
    esp_err_t err = ESP_OK;

    // ESP_LOGW(TAG, "net_idx: %" PRIu16, ble_mesh_key.net_idx);
    // ESP_LOGW(TAG, "app_idx: %" PRIu16, ble_mesh_key.app_idx);
    // ESP_LOGW(TAG, "dst_address: %" PRIu16, dst_address);

    ctx.net_idx = ble_mesh_key.net_idx;
    ctx.app_idx = ble_mesh_key.app_idx;
    ctx.addr = dst_address;
    ctx.send_ttl = MSG_SEND_TTL;
    
    ESP_LOGI(TAG, "Trying to ping root\n");

    err = esp_ble_mesh_client_model_send_msg(client_model, &ctx, opcode, length, data_ptr, MSG_TIMEOUT, true, message_role);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send message to node addr 0x%04x, err_code %d", dst_address, err);
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


static esp_err_t config_complete(esp_ble_mesh_msg_ctx_t ctx) {

    u_int16_t node_addr = ctx.addr;
    config_complete_handler_cb(node_addr);
    return ESP_OK;
}


void send_connectivity_wrapper(void *arg) {
    char connectivity_msg[3] = "C";

    send_connectivity(PROV_OWN_ADDR, strlen(connectivity_msg), (uint8_t *) connectivity_msg);
}

void loop_message_connection() {
    ESP_LOGI(TAG, "----- LOOP MESSAGE STARTED -----\n");
    const esp_timer_create_args_t periodic_timer_args = {
            .callback = &send_connectivity_wrapper,
            // .callback = &periodic_timer_callback,
            /* name is optional, but may help identify the timer when debugging */
            .name = "periodic"
    };

    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));

    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, timer_for_ping));
    ESP_LOGI(TAG, "Started periodic timers, time since boot: %lld us", esp_timer_get_time());
}

void stop_timer() {
    ESP_ERROR_CHECK(esp_timer_delete(periodic_timer));
}

enum State getNodeState() {
    return nodeState;
}

void setNodeState(enum State state) {
    nodeState = state;
    setLEDState(state);
}

void stop_periodic_timer() {
    ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
    ESP_ERROR_CHECK(esp_timer_delete(periodic_timer));
}
// ========================= our function ==================================

static void example_ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                              esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
            ESP_LOGI(TAG, "net_idx 0x%04x, app_idx 0x%04x",
                param->value.state_change.appkey_add.net_idx,
                param->value.state_change.appkey_add.app_idx);
            ESP_LOG_BUFFER_HEX("AppKey", param->value.state_change.appkey_add.app_key, 16);

            custom_model_bind_appkey(param->value.state_change.appkey_add.app_idx);
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
        case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD");
            ESP_LOGI(TAG, "elem_addr 0x%04x, sub_addr 0x%04x, cid 0x%04x, mod_id 0x%04x",
                param->value.state_change.mod_sub_add.element_addr,
                param->value.state_change.mod_sub_add.sub_addr,
                param->value.state_change.mod_sub_add.company_id,
                param->value.state_change.mod_sub_add.model_id);
            break;
        default:
            break;
        }
    }
}

static void print_scan_start_evt(esp_ble_mesh_rpr_server_cb_param_t *param)
{
    ESP_LOGI(TAG, "scan_start, element_idx 0x%02x", param->scan_start.model->element_idx);
    ESP_LOGI(TAG, "scan_start, model_idx 0x%02x", param->scan_start.model->model_idx);
    ESP_LOGI(TAG, "scan_start, scan_items_limit 0x%02x", param->scan_start.scan_items_limit);
    ESP_LOGI(TAG, "scan_start, timeout 0x%02x", param->scan_start.timeout);
    ESP_LOGI(TAG, "scan_start, net_idx 0x%04x", param->scan_start.net_idx);
    ESP_LOGI(TAG, "scan_start, rpr_cli_addr 0x%04x", param->scan_start.rpr_cli_addr);
    ESP_LOG_BUFFER_HEX("CMD_RP: scan_start, uuid", param->scan_start.uuid, 16);
}

static void print_scan_stop_evt(esp_ble_mesh_rpr_server_cb_param_t *param)
{
    ESP_LOGI(TAG, "scan_stop, element_idx 0x%02x", param->scan_stop.model->element_idx);
    ESP_LOGI(TAG, "scan_stop, model_idx 0x%02x", param->scan_stop.model->model_idx);
    ESP_LOGI(TAG, "scan_stop, net_idx 0x%04x", param->scan_stop.net_idx);
    ESP_LOGI(TAG, "scan_stop, rpr_cli_addr 0x%04x", param->scan_stop.rpr_cli_addr);
    ESP_LOG_BUFFER_HEX("CMD_RP: scan_stop, uuid", param->scan_stop.uuid, 16);
}

static void print_ext_scan_start_evt(esp_ble_mesh_rpr_server_cb_param_t *param)
{
    ESP_LOGI(TAG, "ext_scan_start, element_idx 0x%02x", param->ext_scan_start.model->element_idx);
    ESP_LOGI(TAG, "ext_scan_start, model_idx 0x%02x", param->ext_scan_start.model->model_idx);
    if (param->ext_scan_start.ad_type_filter_count && param->ext_scan_start.ad_type_filter) {
        ESP_LOG_BUFFER_HEX("CMD_RP: ext_scan_start, ad_type_filter",
                           param->ext_scan_start.ad_type_filter,
                           param->ext_scan_start.ad_type_filter_count);
    }
    ESP_LOGI(TAG, "ext_scan_start, timeout 0x%02x", param->ext_scan_start.timeout);
    ESP_LOGI(TAG, "ext_scan_start, index 0x%02x", param->ext_scan_start.index);
    ESP_LOGI(TAG, "ext_scan_start, net_idx 0x%04x", param->ext_scan_start.net_idx);
    ESP_LOGI(TAG, "ext_scan_start, rpr_cli_addr 0x%04x", param->ext_scan_start.rpr_cli_addr);
    ESP_LOG_BUFFER_HEX("CMD_RP: ext_scan_start, uuid", param->ext_scan_start.uuid, 16);
}

static void print_ext_scan_stop_evt(esp_ble_mesh_rpr_server_cb_param_t *param)
{
    ESP_LOGI(TAG, "ext_scan_stop, element_idx 0x%02x", param->ext_scan_stop.model->element_idx);
    ESP_LOGI(TAG, "ext_scan_stop, model_idx 0x%02x", param->ext_scan_stop.model->model_idx);
    ESP_LOGI(TAG, "ext_scan_stop, timeout 0x%02x", param->ext_scan_stop.timeout);
    ESP_LOGI(TAG, "ext_scan_stop, index 0x%02x", param->ext_scan_stop.index);
    ESP_LOGI(TAG, "ext_scan_stop, net_idx 0x%04x", param->ext_scan_stop.net_idx);
    ESP_LOGI(TAG, "ext_scan_stop, rpr_cli_addr 0x%04x", param->ext_scan_stop.rpr_cli_addr);
    ESP_LOG_BUFFER_HEX("CMD_RP: ext_scan_stop, uuid", param->ext_scan_stop.uuid, 16);
}

static void print_link_open_evt(esp_ble_mesh_rpr_server_cb_param_t *param)
{
    ESP_LOGI(TAG, "link_open, element_idx 0x%02x", param->link_open.model->element_idx);
    ESP_LOGI(TAG, "link_open, model_idx 0x%02x", param->link_open.model->model_idx);
    ESP_LOGI(TAG, "link_open, status 0x%02x", param->link_open.status);
    ESP_LOGI(TAG, "link_open, timeout 0x%02x", param->link_open.timeout);
    ESP_LOGI(TAG, "link_open, nppi 0x%02x", param->link_open.nppi);
    ESP_LOGI(TAG, "link_open, net_idx 0x%04x", param->link_open.net_idx);
    ESP_LOGI(TAG, "link_open, rpr_cli_addr 0x%04x", param->link_open.rpr_cli_addr);
    ESP_LOG_BUFFER_HEX("CMD_RP: link_open, uuid", param->link_open.uuid, 16);
}

static void print_link_close_evt(esp_ble_mesh_rpr_server_cb_param_t *param)
{
    ESP_LOGI(TAG, "link_close, element_idx 0x%02x", param->link_close.model->element_idx);
    ESP_LOGI(TAG, "link_close, model_idx 0x%02x", param->link_close.model->model_idx);
    ESP_LOGI(TAG, "link_close, nppi 0x%02x", param->link_close.nppi);
    ESP_LOGI(TAG, "link_close, close_by_device %d", param->link_close.close_by_device);
    ESP_LOGI(TAG, "link_close, reason 0x%02x", param->link_close.reason);
    ESP_LOGI(TAG, "link_close, net_idx 0x%04x", param->link_close.net_idx);
    ESP_LOGI(TAG, "link_close, rpr_cli_addr 0x%04x", param->link_close.rpr_cli_addr);
    ESP_LOG_BUFFER_HEX("CMD_RP: link_close, uuid", param->link_close.uuid, 16);
}

static void print_prov_comp_evt(esp_ble_mesh_rpr_server_cb_param_t *param)
{
    ESP_LOGI(TAG, "prov_comp, element_idx 0x%02x", param->prov_comp.model->element_idx);
    ESP_LOGI(TAG, "prov_comp, model_idx 0x%02x", param->prov_comp.model->model_idx);
    ESP_LOGI(TAG, "prov_comp, nppi 0x%02x", param->prov_comp.nppi);
    ESP_LOGI(TAG, "prov_comp, net_idx 0x%04x", param->prov_comp.net_idx);
    ESP_LOGI(TAG, "prov_comp, rpr_cli_addr 0x%04x", param->prov_comp.rpr_cli_addr);
    ESP_LOG_BUFFER_HEX("CMD_RP: prov_comp, uuid", param->prov_comp.uuid, 16);
}

static void example_remote_prov_server_callback(esp_ble_mesh_rpr_server_cb_event_t event,
                                                esp_ble_mesh_rpr_server_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_RPR_SERVER_SCAN_START_EVT:
        ESP_LOGW(TAG, "ESP_BLE_MESH_RPR_SERVER_SCAN_START_EVT");
        print_scan_start_evt(param);
        break;
    case ESP_BLE_MESH_RPR_SERVER_SCAN_STOP_EVT:
        ESP_LOGW(TAG, "ESP_BLE_MESH_RPR_SERVER_SCAN_STOP_EVT");
        print_scan_stop_evt(param);
        break;
    case ESP_BLE_MESH_RPR_SERVER_EXT_SCAN_START_EVT:
        ESP_LOGW(TAG, "ESP_BLE_MESH_RPR_SERVER_EXT_SCAN_START_EVT");
        print_ext_scan_start_evt(param);
        break;
    case ESP_BLE_MESH_RPR_SERVER_EXT_SCAN_STOP_EVT:
        ESP_LOGW(TAG, "ESP_BLE_MESH_RPR_SERVER_EXT_SCAN_STOP_EVT");
        print_ext_scan_stop_evt(param);
        break;
    case ESP_BLE_MESH_RPR_SERVER_LINK_OPEN_EVT:
        ESP_LOGW(TAG, "ESP_BLE_MESH_RPR_SERVER_LINK_OPEN_EVT");
        print_link_open_evt(param);
        break;
    case ESP_BLE_MESH_RPR_SERVER_LINK_CLOSE_EVT:
        ESP_LOGW(TAG, "ESP_BLE_MESH_RPR_SERVER_LINK_CLOSE_EVT");
        print_link_close_evt(param);
        break;
    case ESP_BLE_MESH_RPR_SERVER_PROV_COMP_EVT:
        ESP_LOGW(TAG, "ESP_BLE_MESH_RPR_SERVER_PROV_COMP_EVT");
        print_prov_comp_evt(param);
        break;
    default:
        break;
    }
}

static esp_err_t ble_mesh_init(void)
{
    esp_err_t err;
    
    // ble_mesh_key.net_idx = ESP_BLE_MESH_KEY_PRIMARY;
    ble_mesh_key.app_idx = APP_KEY_IDX;

    esp_ble_mesh_register_prov_callback(ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_server_callback(example_ble_mesh_config_server_cb);
    esp_ble_mesh_register_custom_model_callback(ble_mesh_custom_model_cb);
    esp_ble_mesh_register_rpr_server_callback(example_remote_prov_server_callback);

    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mesh stack");
        return err;
    }

    err = esp_ble_mesh_client_model_init(&vnd_models[0]);
    if (err) {
        ESP_LOGE(TAG, "Failed to initialize vendor client");
        return err;
    }

    err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh node");
        return err;
    }

    ESP_LOGI(TAG, "BLE Mesh Node initialized");

    return ESP_OK;
}

// static void periodic_state_callback(void* arg)
// {
//     int64_t time_since_boot = esp_timer_get_time();
//     // ESP_LOGI(TAG, "Current Node State: %d", nodeState);
// }

void reset_esp32()
{
    // order edge module to restart since network is about to get refreshed
    char edge_restart_message[20] = "RST";
    uint16_t msg_length = strlen(edge_restart_message);
    broadcast_message(msg_length, (uint8_t *)edge_restart_message);

#if CONFIG_BLE_MESH_SETTINGS
    // erase the persistent memory
    esp_err_t error = ESP_OK;
    error = esp_ble_mesh_provisioner_direct_erase_settings();
#endif /* CONFIG_BLE_MESH_SETTINGS */
    uart_sendMsg(0, "Persistent Memory Reseted, Should Restart Module Later\n");
}

static void oneshot_timer_callback(void* arg)
{
    int64_t time_since_boot = esp_timer_get_time();
    ESP_LOGI(TAG, "One-shot timer called, time since boot: %lld us", time_since_boot);
    setLEDState(getNodeState());
}

esp_err_t esp_module_edge_init(
    void (*prov_complete_handler)(uint16_t node_index, const esp_ble_mesh_octet16_t uuid, uint16_t addr, uint8_t element_num, uint16_t net_idx),
    void (*config_complete_handler)(uint16_t addr),
    void (*recv_message_handler)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr),
    void (*recv_response_handler)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr),
    void (*timeout_handler)(esp_ble_mesh_msg_ctx_t *ctx, uint32_t opcode),
    void (*broadcast_handler)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr),
    void (*connectivity_handler)(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr)
) {
    esp_err_t err;

    ESP_LOGI(TAG, "Initializing EDGE Module...");

    // attach application level callback
    prov_complete_handler_cb = prov_complete_handler;
    config_complete_handler_cb = config_complete_handler;
    recv_message_handler_cb = recv_message_handler;
    recv_response_handler_cb = recv_response_handler;
    timeout_handler_cb = timeout_handler;
    broadcast_handler_cb = broadcast_handler;
    connectivity_handler_cb = connectivity_handler;
    if (prov_complete_handler_cb == NULL || recv_message_handler_cb == NULL || recv_response_handler_cb == NULL || 
        timeout_handler_cb == NULL || broadcast_handler_cb == NULL || timeout_handler_cb == NULL || connectivity_handler_cb == NULL) {
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
    // const esp_timer_create_args_t periodic_state_args = {
    //         .callback = &periodic_state_callback,
    //         /* name is optional, but may help identify the timer when debugging */
    //         .name = "state"
    // };
    // esp_timer_handle_t periodic_state;
    // ESP_ERROR_CHECK(esp_timer_create(&periodic_state_args, &periodic_state));
    
    // ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_state, 1000000));
    // ESP_LOGI(TAG, "Started timers, time since boot: %lld us", esp_timer_get_time());

    // /* Let the timer run for a little bit more */
    // usleep(20000000);

    // /* Clean up and finish the example */
    // ESP_ERROR_CHECK(esp_timer_stop(periodic_state));
    // ESP_ERROR_CHECK(esp_timer_delete(periodic_state));
    // ESP_LOGI(TAG, "Stopped and deleted timers");

    //A timer to active the node state LED
    const esp_timer_create_args_t oneshot_timer_args = {
                .callback = &oneshot_timer_callback,
                /* argument specified here will be passed to timer callback function */
                .name = "one-shot"
    };
    esp_timer_handle_t oneshot_timer;
    ESP_ERROR_CHECK(esp_timer_create(&oneshot_timer_args, &oneshot_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(oneshot_timer, 3000000));

    return ESP_OK;
}