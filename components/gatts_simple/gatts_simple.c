#include "gatts_simple.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gatt_common_api.h"
#include "esp_check.h"
#include "string.h"
#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/semphr.h"

struct gatts_simple_service_data {
    SemaphoreHandle_t mutex;
    esp_gatt_srvc_id_t service_id;
    struct gatts_simple_service_data* next_service_data;
};

struct gatts_simple_app_data {
    uint16_t app_id;
    esp_gatt_if_t gatt_if;
    struct gatts_simple_service_data* service_list_root;
    char* device_name;
    SemaphoreHandle_t mutex;
    struct gatts_simple_app_data* next_app_data;
};

const char* gatts_tag = "gatts_simple";
struct gatts_simple_app_data app_list_root = {
    .next_app_data = NULL,
};

void uuid_from_str(uint8_t* uuid_dest, const char* uuid_str) {
    uint8_t uuid_buf[16];
    uint8_t nib_num = 0;
    uint8_t string_len = strlen(uuid_str);
    for (int i=0; i<string_len && nib_num < 32; i++) {
        uint8_t my_nibble = 0;
        char myChar = uuid_str[i];
        if (myChar >= '0' && myChar <= '9') {
            my_nibble = myChar - '0';
        } else if (myChar >= 'a' && myChar <= 'f') {
            my_nibble = myChar - 'a' + 10;
        } else if (myChar >= 'A' && myChar <= 'F') {
            my_nibble = myChar - 'A' + 10;
        } else {
            continue;
        }
        uuid_buf[15 - (nib_num/2)] = (uuid_buf[15 - (nib_num/2)] << 4) | my_nibble;
        nib_num++;
    }
    memcpy(uuid_dest, uuid_buf, 16);
}

// creates a simple bluetooth app, with no characteristics, and advertising data
// stores a pointer in hdl to be used in future calls to this library for this app
// app_id must be unique from any previously created apps
esp_err_t gatts_simple_create_app(gatts_simple_app_handle_t* hdl, gatts_simple_app_def_t* app_def) {
    esp_err_t ret = ESP_OK;
    if (hdl == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *hdl = malloc(sizeof(struct gatts_simple_app_data));
    if (*hdl == NULL) {
        return ESP_ERR_NO_MEM;
    }
    (*hdl)->mutex = xSemaphoreCreateMutex();
    (*hdl)->app_id = app_def->app_id;
    (*hdl)->next_app_data = NULL;
    (*hdl)->service_list_root = NULL;
    uint8_t name_len = strlen(app_def->device_name);
    (*hdl)->device_name = malloc(name_len + 1);
    if ((*hdl)->device_name == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto err;
    }
    memcpy((*hdl)->device_name, app_def->device_name, name_len + 1);
    struct gatts_simple_app_data* cur_item = &app_list_root;
    while (cur_item->next_app_data != NULL) {
        cur_item = cur_item->next_app_data;
    }
    cur_item->next_app_data = *hdl;
    return esp_ble_gatts_app_register((*hdl)->app_id);
    
    err:
    if ((*hdl)->device_name != NULL) {
        free((*hdl)->device_name);
    }
    return ret;
}

esp_err_t gatts_simple_add_service(gatts_simple_app_handle_t hdl_app, gatts_simple_service_handle_t* hdl_srv, 
        esp_bt_uuid_t* uuid) {
    esp_err_t ret = ESP_OK;
    (*hdl_srv) = malloc(sizeof(struct gatts_simple_service_data));
    if ((*hdl_srv) == NULL) {
        return ESP_ERR_NO_MEM;
    }
    (*hdl_srv)->mutex = xSemaphoreCreateMutex();
    (*hdl_srv)->service_id.id.uuid = *uuid;
    ESP_LOG_BUFFER_HEX("uuid: ", (*hdl_srv)->service_id.id.uuid.uuid.uuid128, 16);
    if(xSemaphoreTake(hdl_app->mutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
        if(xSemaphoreTake((*hdl_srv)->mutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
            ret = esp_ble_gatts_create_service(hdl_app->gatt_if, &(*hdl_srv)->service_id, 1);
            xSemaphoreGive((*hdl_srv)->mutex);
            xSemaphoreGive(hdl_app->mutex);
        } else {
            ret = ESP_FAIL;
            xSemaphoreGive(hdl_app->mutex);
            ESP_LOGE(gatts_tag, "add service: failed to get service mutex.  This should never happen");
        }
    } else {
        ESP_LOGI(gatts_tag, "add service: failed to get app mutex.  Maybe busy and will be done later...");
    }
    return ret;
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(gatts_tag, "GATT server register, status %d, app_id %d, gatts_if %d", param->reg.status, param->reg.app_id, gatts_if);
        struct gatts_simple_app_data* cur_app = app_list_root.next_app_data;
        while (cur_app != NULL && cur_app->app_id != param->reg.app_id) {
            cur_app = cur_app->next_app_data;
        }
        if (cur_app == NULL) {
            ESP_LOGE(gatts_tag, "GATT server register: do not recognize");
            break;
        }
        if (param->reg.status != ESP_GATT_OK) {
            ESP_LOGE(gatts_tag, "GATT server register: bad status");
            break;
        }
        cur_app->gatt_if = gatts_if;
        xSemaphoreGive(cur_app->mutex);
        break;
    case ESP_GATTS_READ_EVT:
        ESP_LOGI(gatts_tag, "Characteristic read, conn_id %d, trans_id %" PRIu32 ", handle %d, offset:%d",
                param->read.conn_id, param->read.trans_id, param->read.handle, param->read.offset);
        break;
    case ESP_GATTS_EXEC_WRITE_EVT:
        ESP_LOGI(gatts_tag, "Execute write");
        break;
    case ESP_GATTS_UNREG_EVT:
        ESP_LOGI(gatts_tag, "Unregister");
        break;
    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(gatts_tag, "Service create, status %d, service_handle %d", param->create.status, param->create.service_handle);
        break;
    case ESP_GATTS_ADD_INCL_SRVC_EVT:
        ESP_LOGI(gatts_tag, "Added included service, status %d, attr_handle %d, service_handle %d",
            param->add_incl_srvc.status, param->add_incl_srvc.attr_handle, param->add_incl_srvc.service_handle);
        break;
    case ESP_GATTS_ADD_CHAR_EVT:
        ESP_LOGI(gatts_tag, "Characteristic add, status %d, attr_handle %d service_handle %d",
            param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);
        break;
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        ESP_LOGI(gatts_tag, "Descriptor add, status %d, attr_handle %d, service_handle %d",
            param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
        break;
    case ESP_GATTS_DELETE_EVT:
        ESP_LOGI(gatts_tag, "Delete status %d, service_handle  %d", param->del.status, param->del.service_handle);
        break;
    case ESP_GATTS_START_EVT:
        ESP_LOGI(gatts_tag, "service start, status %d, service_handle %d",
            param->start.status, param->start.service_handle);
        break;
    case ESP_GATTS_STOP_EVT:
        ESP_LOGI(gatts_tag, "service stop, status %d, service_handle %d",
            param->stop.status, param->stop.service_handle);
        break;
    case ESP_GATTS_CONNECT_EVT: {
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(gatts_tag, "Connected, conn_id %u, remote "ESP_BD_ADDR_STR"",
            param->connect.conn_id, ESP_BD_ADDR_HEX(param->connect.remote_bda));
        break;
    }
    case ESP_GATTS_DISCONNECT_EVT: 
        ESP_LOGI(gatts_tag, "Disconnected, remote"ESP_BD_ADDR_STR", reason0x%02x",
            ESP_BD_ADDR_HEX(param->disconnect.remote_bda), param->disconnect.reason);
        break;
    case ESP_GATTS_CONF_EVT:
        ESP_LOGI(gatts_tag, "Confirm receive, status %d, attr_handle %d", param->conf.status, param->conf.handle);
        if (param->conf.status != ESP_GATT_OK) {
            ESP_LOG_BUFFER_HEX(gatts_tag, param->conf.value, param->conf.len);
        }
        break;
    case ESP_GATTS_OPEN_EVT:
        ESP_LOGI(gatts_tag, "Open status %d", param->open.status);
        break;
    case ESP_GATTS_CLOSE_EVT:
        ESP_LOGI(gatts_tag, "Close status %d, conn_id %d", param->close.status, param->close.conn_id);
        break;
    default:
        ESP_LOGI(gatts_tag, "Unrecognized gatts event: %d", event);
        break;
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        ESP_LOGI(gatts_tag, "Raw Adv Data Complete");
        /*
        adv_config_done &= (~adv_config_flag);
        if (adv_config_done==0){
            esp_ble_gap_start_advertising(&adv_params);
        }
            */
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        ESP_LOGI(gatts_tag, "RAW adv rsp data complete");
        /*
        adv_config_done &= (~scan_rsp_config_flag);
        if (adv_config_done==0){
            esp_ble_gap_start_advertising(&adv_params);
        }
            */
        break;
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(gatts_tag, "Adv data set compete");
        /*
        adv_config_done &= (~adv_config_flag);
        if (adv_config_done == 0){
            esp_ble_gap_start_advertising(&adv_params);
        }
            */
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(gatts_tag, "Scan resp data set complete");
        /*
        adv_config_done &= (~scan_rsp_config_flag);
        if (adv_config_done == 0){
            esp_ble_gap_start_advertising(&adv_params);
        }
            */
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        //advertising start complete event to indicate advertising start successfully or failed
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(gatts_tag, "Advertising start failed, status %d", param->adv_start_cmpl.status);
            break;
        }
        ESP_LOGI(gatts_tag, "Advertising start successfully");
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(gatts_tag, "Advertising stop failed, status %d", param->adv_stop_cmpl.status);
            break;
        }
        ESP_LOGI(gatts_tag, "Advertising stop successfully");
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
         ESP_LOGI(gatts_tag, "Connection params update, status %d, conn_int %d, latency %d, timeout %d",
                  param->update_conn_params.status,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
        break;
    case ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT:
        ESP_LOGI(gatts_tag, "Packet length update, status %d, rx %d, tx %d",
                  param->pkt_data_length_cmpl.status,
                  param->pkt_data_length_cmpl.params.rx_len,
                  param->pkt_data_length_cmpl.params.tx_len);
        break;
    default:
        break;
    }
}

esp_err_t gatts_simple_init() {
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_bt_controller_init(&bt_cfg), gatts_tag, "esp_bt_controller_init() failed");
    ESP_RETURN_ON_ERROR(esp_bt_controller_enable(ESP_BT_MODE_BLE), gatts_tag, "failed to enable bt controller");
    ESP_RETURN_ON_ERROR(esp_bluedroid_init(), gatts_tag, "esp_bluedroid_init() failed");
    ESP_RETURN_ON_ERROR(esp_bluedroid_enable(), gatts_tag, "esp_bluedroid_enable() failed");
    ESP_RETURN_ON_ERROR(esp_ble_gatts_register_callback(gatts_event_handler), 
        gatts_tag, "esp_ble_gatts_register_callback() failed");
    ESP_RETURN_ON_ERROR(esp_ble_gap_register_callback(gap_event_handler), 
        gatts_tag, "exp_ble_gap_register_callback() failed");
    return ESP_OK;
}