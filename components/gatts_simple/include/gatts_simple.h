#ifndef BLE_GATTS_SIMPLE_H
#define BLE_GATTS_SIMPLE_H

#include "esp_err.h"
#include "esp_bt_defs.h"
#include "esp_gatt_defs.h"
#include "esp_gap_ble_api.h"

typedef struct gatts_simple_app_data*       gatts_simple_app_handle_t;
typedef struct gatts_simple_service_data*   gatts_simple_service_handle_t;
typedef struct gatts_simple_char_data*      gatts_simple_char_handle_t;

typedef struct {
    uint16_t app_id;
    char* device_name;
} gatts_simple_app_def_t;

esp_err_t gatts_simple_init();
esp_err_t gatts_simple_create_app(gatts_simple_app_handle_t* hdl, gatts_simple_app_def_t* app_def);
esp_err_t gatts_simple_add_service(gatts_simple_service_handle_t* hdl_srv, gatts_simple_app_handle_t hdl_app,
        const char* uuid);
esp_err_t gatts_simple_add_char(gatts_simple_char_handle_t* char_hdl, gatts_simple_service_handle_t serv_hdl,
const char* uuid_str, esp_gatt_perm_t perm, esp_gatt_char_prop_t prop, esp_attr_value_t* value, 
        esp_attr_control_t* control);
esp_err_t gatts_simple_advertise(esp_ble_adv_params_t* adv_params, 
        esp_ble_adv_data_t* adv_data, esp_ble_adv_data_t* rsp_data, const char* dev_name);
esp_err_t gatts_simple_advertise_raw(esp_ble_adv_params_t* adv_params, 
        uint8_t* adv_data, uint16_t adv_data_size, 
        uint8_t* rsp_data, uint16_t rsp_data_size);

#endif