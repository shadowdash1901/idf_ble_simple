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

#define GATTS_SIMPLE_MUTEX_TIMEOUT 200

#define ESP_UUID128_STR "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x"
#define ESP_UUID128_HEX(uuid)   uuid[15], uuid[14], uuid[13], uuid[12], \
                                uuid[11], uuid[10], uuid[9],  uuid[8],  uuid[7], uuid[6], \
                                uuid[5],  uuid[4],  uuid[3],  uuid[2],  uuid[1], uuid[0] 

struct gatts_simple_app_data {
    uint16_t app_id;
    esp_gatt_if_t gatt_if;
    char* device_name;
    struct gatts_simple_app_data* next_app_data;
};

struct gatts_simple_service_data {
    esp_gatt_srvc_id_t service_id;
    uint16_t service_handle;
    uint16_t parent_app_id;
    bool create_pending;
    struct gatts_simple_service_data* next_service_data;
};

struct gatts_simple_char_data {
    uint16_t parent_service_id;
    uint16_t char_handle;
    esp_bt_uuid_t uuid;
    esp_gatt_perm_t permissions;
    esp_gatt_char_prop_t properties;
    esp_attr_value_t* value;
    esp_attr_control_t* control;
    bool create_pending;
    struct gatts_simple_char_data* next_char_data;
};

const char* gatts_tag = "gatts_simple";
struct gatts_simple_app_data app_list_root = {
    .next_app_data = NULL,
};
struct gatts_simple_service_data service_list_root = {
    .next_service_data = NULL,
};
struct gatts_simple_char_data char_list_root = {
    .next_char_data = NULL,
};
SemaphoreHandle_t gatts_simple_mutex;

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

esp_err_t add_app_data(struct gatts_simple_app_data** hdl, uint16_t app_id, char* device_name) {
    struct gatts_simple_app_data* app_data = &app_list_root;
    while (app_data->next_app_data != NULL) {
        if (app_data->next_app_data->app_id == app_id) {
            ESP_LOGE(gatts_tag, "add_app_data: list already contains app_id:%d", app_id);
            return ESP_ERR_INVALID_ARG;
        }
        app_data = app_data->next_app_data;
    }
    *hdl = malloc(sizeof(struct gatts_simple_app_data));
    if (*hdl == NULL) {
        return ESP_ERR_NO_MEM;
    }
    app_data->next_app_data = *hdl;
    (*hdl)->app_id = app_id;
    (*hdl)->next_app_data = NULL;
    uint16_t name_len = strlen(device_name) + 1;
    (*hdl)->device_name = malloc(name_len);
    if ((*hdl)->device_name == NULL) {
        free(*hdl);
        (*hdl) = NULL;
        return ESP_ERR_NO_MEM;
    }
    memcpy((*hdl)->device_name, device_name, name_len);
    return ESP_OK;
}

esp_err_t remove_app_data(uint16_t app_id) {
    struct gatts_simple_app_data* cur_data = app_list_root.next_app_data;
    while (cur_data->next_app_data != NULL) {
        if (cur_data->next_app_data->app_id == app_id) {
            struct gatts_simple_app_data* del = cur_data->next_app_data;
            cur_data->next_app_data->next_app_data = del->next_app_data;
            free(del->device_name);
            free(del);
        }
    }
    return ESP_OK;
}

esp_err_t get_app_data(struct gatts_simple_app_data** app_data, uint16_t app_id) {
    struct gatts_simple_app_data* cur_data = app_list_root.next_app_data;
    while (cur_data != NULL && cur_data->app_id != app_id) {
        cur_data = cur_data->next_app_data;
    }
    *app_data = cur_data;
    return (cur_data == NULL) ? ESP_FAIL : ESP_OK;
}

// creates a simple bluetooth app, with no characteristics, and advertising data
// stores a pointer in hdl to be used in future calls to this library for this app
// app_id must be unique from any previously created apps
esp_err_t gatts_simple_create_app(gatts_simple_app_handle_t* hdl, gatts_simple_app_def_t* app_def) {
    if (hdl == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!xSemaphoreTake(gatts_simple_mutex, GATTS_SIMPLE_MUTEX_TIMEOUT)) {
        return ESP_ERR_TIMEOUT;
    }
    add_app_data(hdl, app_def->app_id, app_def->device_name);
    return esp_ble_gatts_app_register((*hdl)->app_id);
}

esp_err_t add_service_data(struct gatts_simple_service_data** data, uint16_t app_id, esp_bt_uuid_t* uuid) {
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *data = malloc(sizeof(struct gatts_simple_service_data));
    if (*data == NULL) {
        ESP_LOGE(gatts_tag, "Failed to allocate data for new service");
        return ESP_ERR_NO_MEM;
    }
    (*data)->service_id.id.uuid = *uuid;
    (*data)->next_service_data = NULL;
    (*data)->parent_app_id = app_id;
    struct gatts_simple_service_data* cur_data = &service_list_root;
    while (cur_data->next_service_data != NULL) {
        cur_data = cur_data->next_service_data;
    }
    cur_data->next_service_data = *data;
    return ESP_OK;
}

esp_err_t remove_service_data(uint16_t app_id, struct gatts_simple_service_data* del_data) {
    struct gatts_simple_service_data* service_data = &service_list_root;
    while (service_data->next_service_data != NULL) {
        if (service_data->next_service_data == del_data) {
            del_data = service_data->next_service_data;
            service_data->next_service_data = del_data->next_service_data;
            free(del_data);
            break;
        }
    }
    return ESP_OK;
}

esp_err_t gatts_simple_add_service(gatts_simple_service_handle_t* hdl_srv, gatts_simple_app_handle_t hdl_app, 
        const char* uuid_str) {
    esp_err_t ret = ESP_OK;
    if (xSemaphoreTake(gatts_simple_mutex, GATTS_SIMPLE_MUTEX_TIMEOUT) != pdTRUE) {
        ESP_LOGE(gatts_tag, "gatts_simple_add_service: failed to get mutex");
        return ESP_ERR_TIMEOUT;
    }
    esp_bt_uuid_t uuid = {
        .len = ESP_UUID_LEN_128,
    };
    uuid_from_str(uuid.uuid.uuid128, uuid_str);
    ESP_LOGI(gatts_tag, "Service UUID "ESP_UUID128_STR, ESP_UUID128_HEX(uuid.uuid.uuid128));
    ESP_RETURN_ON_ERROR(add_service_data(hdl_srv, hdl_app->app_id, &uuid), gatts_tag, "Failed to create service data");
    (*hdl_srv)->create_pending = true;
    ret = esp_ble_gatts_create_service(hdl_app->gatt_if, &(*hdl_srv)->service_id, 1);
    return ret;
}

esp_err_t add_char_data(struct gatts_simple_char_data** char_hdl, uint16_t parent_service_id, 
        esp_bt_uuid_t* uuid, esp_gatt_perm_t permissions, esp_gatt_char_prop_t properties, 
        esp_attr_value_t* value, esp_attr_control_t* control) {
    struct gatts_simple_char_data* char_data = &char_list_root;
    while (char_data->next_char_data != NULL) {
        char_data = char_data->next_char_data;
    }
    char_data->next_char_data = malloc(sizeof(struct gatts_simple_char_data));
    if (char_data->next_char_data == NULL) {
        return ESP_ERR_NO_MEM;
    }
    char_data = char_data->next_char_data;
    char_data->uuid = *uuid;
    char_data->permissions = permissions;
    char_data->properties = properties;
    char_data->value = value;
    char_data->control = control;
    *char_hdl = char_data;
    return ESP_OK;
}

esp_err_t gatts_simple_add_char(gatts_simple_char_handle_t* char_hdl, gatts_simple_service_handle_t serv_hdl,
        const char* uuid_str, esp_gatt_perm_t perm, esp_gatt_char_prop_t prop, esp_attr_value_t* value, 
        esp_attr_control_t* control) {
    esp_err_t ret = ESP_OK;
    if (char_hdl == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_bt_uuid_t uuid = {
        .len = ESP_UUID_LEN_128,
    };
    uuid_from_str(uuid.uuid.uuid128, uuid_str);
    ESP_LOGI(gatts_tag, "Char uuid: "ESP_UUID128_STR, ESP_UUID128_HEX(uuid.uuid.uuid128));
    if (xSemaphoreTake(gatts_simple_mutex, GATTS_SIMPLE_MUTEX_TIMEOUT) != pdTRUE) {
        ESP_LOGE(gatts_tag, "gatts_simple_add_char: failed to get mutex");
        return ESP_ERR_TIMEOUT;
    }
    ESP_RETURN_ON_ERROR(add_char_data(char_hdl, serv_hdl->service_handle, &uuid, perm, prop, value, control),
        gatts_tag, "gatts_simple_char: failed to add char");
    ESP_LOGI(gatts_tag, "Adding characteristic srv_hdl:%d perm:%d prop:%d", serv_hdl->service_handle, perm, prop);
    ret = esp_ble_gatts_add_char(serv_hdl->service_handle, &uuid, perm, prop, value, control);
    if (ret != ESP_OK) {
        xSemaphoreGive(gatts_simple_mutex);
    }
    return ret;
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param) {
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        ESP_LOGI(gatts_tag, "GATT server register, status %d, app_id %d, gatts_if %d", param->reg.status, param->reg.app_id, gatts_if);
        struct gatts_simple_app_data* app_data;
        if (get_app_data(&app_data, param->reg.app_id) != ESP_OK) {
            ESP_LOGE(gatts_tag, "GATT server register: do not recognize");
        }
        if (param->reg.status != ESP_GATT_OK) {
            remove_app_data(param->reg.app_id);
            ESP_LOGE(gatts_tag, "GATT server register: bad status");
            break;
        }
        app_data->gatt_if = gatts_if;
        xSemaphoreGive(gatts_simple_mutex);
        break;
    }
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
    case ESP_GATTS_CREATE_EVT: {
        ESP_LOGI(gatts_tag, "Service create, status %d, service_handle %d", param->create.status, param->create.service_handle);
        struct gatts_simple_service_data* service_data = service_list_root.next_service_data;
        while (service_data != NULL && !service_data->create_pending) {
            service_data = service_data->next_service_data;
        }
        if (service_data != NULL) {
            service_data->service_id = param->create.service_id;
            service_data->service_handle = param->create.service_handle;
            service_data->create_pending = false;
            //esp_ble_gatts_start_service(service_data->service_handle);
            xSemaphoreGive(gatts_simple_mutex);
        } else {
            ESP_LOGE(gatts_tag, "gatts_event_handler: service create: service not recognized");
        }
        break;
    }
    case ESP_GATTS_ADD_INCL_SRVC_EVT:
        ESP_LOGI(gatts_tag, "Added included service, status %d, attr_handle %d, service_handle %d",
            param->add_incl_srvc.status, param->add_incl_srvc.attr_handle, param->add_incl_srvc.service_handle);
        break;
    case ESP_GATTS_ADD_CHAR_EVT:
        ESP_LOGI(gatts_tag, "Characteristic add, status %d, attr_handle %d service_handle %d",
            param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);
        xSemaphoreGive(gatts_simple_mutex);
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
        xSemaphoreGive(gatts_simple_mutex);
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
        ESP_LOGI(gatts_tag, "Raw adv Data Complete");
        xSemaphoreGive(gatts_simple_mutex);
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        ESP_LOGI(gatts_tag, "Raw rsp data complete");
        xSemaphoreGive(gatts_simple_mutex);
        break;
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(gatts_tag, "Adv data set compete");
        xSemaphoreGive(gatts_simple_mutex);
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(gatts_tag, "Scan resp data set complete");
        xSemaphoreGive(gatts_simple_mutex);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        //advertising start complete event to indicate advertising start successfully or failed
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(gatts_tag, "Advertising start failed, status %d", param->adv_start_cmpl.status);
            break;
        }
        ESP_LOGI(gatts_tag, "Advertising start successfully");
        xSemaphoreGive(gatts_simple_mutex);
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
    ESP_RETURN_ON_ERROR(esp_ble_gatt_set_local_mtu(500), gatts_tag, "failed to set local mtu");
    ESP_RETURN_ON_ERROR(esp_ble_gatts_register_callback(gatts_event_handler), 
        gatts_tag, "esp_ble_gatts_register_callback() failed");
    ESP_RETURN_ON_ERROR(esp_ble_gap_register_callback(gap_event_handler), 
        gatts_tag, "esp_ble_gap_register_callback() failed");
    gatts_simple_mutex = xSemaphoreCreateBinary();
    xSemaphoreGive(gatts_simple_mutex);
    return ESP_OK;
}

esp_err_t gatts_simple_advertise_raw(esp_ble_adv_params_t* adv_params, 
        uint8_t* adv_data, uint16_t adv_data_size, 
        uint8_t* rsp_data, uint16_t rsp_data_size) {
    if (xSemaphoreTake(gatts_simple_mutex, GATTS_SIMPLE_MUTEX_TIMEOUT) != pdTRUE) {
        ESP_LOGE(gatts_tag, "gatts_simple_advertise_raw: failed to take mutex to configure adv raw");
        return ESP_ERR_TIMEOUT;
    }
    esp_ble_gap_config_adv_data_raw(adv_data, adv_data_size);
    if (xSemaphoreTake(gatts_simple_mutex, GATTS_SIMPLE_MUTEX_TIMEOUT) != pdTRUE) {
        ESP_LOGE(gatts_tag, "gatts_simple_advertise_raw: failed to take mutex to configure rsp raw");
        return ESP_ERR_TIMEOUT;
    }
    esp_ble_gap_config_scan_rsp_data_raw(rsp_data, rsp_data_size);
    if (xSemaphoreTake(gatts_simple_mutex, GATTS_SIMPLE_MUTEX_TIMEOUT) != pdTRUE) {
        ESP_LOGE(gatts_tag, "gatts_simple_advertise_raw: failed to take mutex to configure rsp raw");
        return ESP_ERR_TIMEOUT;
    }
    esp_ble_gap_start_advertising(adv_params);
    return ESP_OK;
}

esp_err_t gatts_simple_advertise(esp_ble_adv_params_t* adv_params, 
        esp_ble_adv_data_t* adv_data, esp_ble_adv_data_t* rsp_data, const char* dev_name) {
    if (xSemaphoreTake(gatts_simple_mutex, GATTS_SIMPLE_MUTEX_TIMEOUT) != pdTRUE) {
        ESP_LOGE(gatts_tag, "gatts_simple_advertise: failed to take mutex to configure adv data");
        return ESP_ERR_TIMEOUT;
    }
    esp_ble_gap_set_device_name(dev_name);
    esp_ble_gap_config_adv_data(adv_data);
    if (xSemaphoreTake(gatts_simple_mutex, GATTS_SIMPLE_MUTEX_TIMEOUT) != pdTRUE) {
        ESP_LOGE(gatts_tag, "gatts_simple_advertise: failed to take mutex to advertise rsp data");
        return ESP_ERR_TIMEOUT;
    }
    esp_ble_gap_config_adv_data(rsp_data);
    if (xSemaphoreTake(gatts_simple_mutex, GATTS_SIMPLE_MUTEX_TIMEOUT) != pdTRUE) {
        ESP_LOGE(gatts_tag, "gatts_simple_advertise: failed to take mutex to advertise rsp data");
        return ESP_ERR_TIMEOUT;
    }
    esp_ble_gap_start_advertising(adv_params);

    return ESP_OK;
}