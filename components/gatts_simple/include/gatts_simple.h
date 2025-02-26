#ifndef BLE_GATTS_SIMPLE_H
#define BLE_GATTS_SIMPLE_H

#include "esp_err.h"
#include "esp_bt_defs.h"

typedef struct gatts_simple_app_data* gatts_simple_app_handle_t;
typedef struct gatts_simple_service_data* gatts_simple_service_handle_t;

typedef struct {
    uint16_t app_id;
    char* device_name;
} gatts_simple_app_def_t;

esp_err_t gatts_simple_init();
esp_err_t gatts_simple_create_app(gatts_simple_app_handle_t* hdl, gatts_simple_app_def_t* app_def);
esp_err_t gatts_simple_add_service(gatts_simple_app_handle_t hdl_app, gatts_simple_service_handle_t* hdl_srv, 
        esp_bt_uuid_t* uuid);
void uuid_from_str(uint8_t* uuid_dest, const char* uuid_str);

#endif