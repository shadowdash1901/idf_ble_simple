#include <stdio.h>
#include "esp_log.h"
#include "gatts_simple.h"
#include "esp_check.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

gatts_simple_app_handle_t app0_hdl;
gatts_simple_app_handle_t app1_hdl;
gatts_simple_service_handle_t app0_srv0_hdl;
gatts_simple_service_handle_t app0_srv1_hdl;
gatts_simple_char_handle_t app0_srv0_char0_hdl;
gatts_simple_char_handle_t app0_srv0_char1_hdl;

gatts_simple_app_def_t app0_def = {
    .app_id = 0,
    .device_name = "app0",
};
const char* app0_serv0_str = "df0b4f24-cb71-4ec0-b43e-7f6fc3a3440f";
const char* app0_serv1_str = "0827e3bc-123a-46b7-b2dd-c7bfc02e8dcb";

const char* app0_serv0_char0_str = "2b6513d4-5453-4d15-af7f-98788ffe9e51";
esp_gatt_perm_t app0_serv0_char0_perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE;
esp_gatt_char_prop_t app0_serv0_char0_prop = 
        ESP_GATT_CHAR_PROP_BIT_READ |
        ESP_GATT_CHAR_PROP_BIT_WRITE |
        ESP_GATT_CHAR_PROP_BIT_NOTIFY;
char app0_serv0_char0_val_str[200];
esp_attr_value_t app0_serv0_char0_val = {
    .attr_len = 1,
    .attr_max_len = sizeof(app0_serv0_char0_val_str),
    .attr_value = (uint8_t*)app0_serv0_char0_val_str,
};
esp_attr_control_t app0_serv0_char0_ctrl;

const char* app0_serv0_char1_str = "5720fe2d-8603-4429-b2d0-d3764b6fb57a";
esp_gatt_perm_t app0_serv0_char1_perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE;
esp_gatt_char_prop_t app0_serv0_char1_prop = 
        ESP_GATT_CHAR_PROP_BIT_BROADCAST | 
        ESP_GATT_CHAR_PROP_BIT_READ |
        ESP_GATT_CHAR_PROP_BIT_WRITE |
        ESP_GATT_CHAR_PROP_BIT_NOTIFY;
char app0_serv0_char1_val_str[200];
esp_attr_value_t app0_serv0_char1_val = {
    .attr_len = 0,
    .attr_max_len = sizeof(app0_serv0_char1_val_str),
    .attr_value = (uint8_t*)app0_serv0_char1_val_str,
};
esp_attr_control_t app0_serv0_char1_ctrl;

gatts_simple_app_def_t app1_def = {
    .app_id = 1,
    .device_name="app1",
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .service_data_len = 0,
    .service_uuid_len = 0,
};

uint8_t uuid_arr[] = {
    // app0_serv0 uuid = df0b4f24-cb71-4ec0-b43e-7f6fc3a3440f
    0x0f, 0x44, 0xa3, 0xc3, 0x6f, 0x7f,
    0x3e, 0xb4, 0xc0, 0x4e, 0x71, 0xcb,
    0x24, 0x4f, 0x0b, 0xdf,
    
    // app0_serv1_uuid = 0827e3bc-123a-46b7-b2dd-c7bfc02e8dcb
    0xcb, 0x8d, 0x2e, 0xc0, 0xbf, 0xc7,
    0xdd, 0xb2, 0xb7, 0x46, 0x3a, 0x12,
    0xbc, 0xe3, 0x27, 0x08,
};

static esp_ble_adv_data_t rsp_data = {
    .set_scan_rsp = true,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
    .include_name = false,
    .include_txpower = false,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .service_data_len = 0,
    .service_uuid_len = sizeof(uuid_arr),
    .p_service_uuid = uuid_arr
};

static uint8_t adv_data_raw[] = {
    0x02, ESP_BLE_AD_TYPE_FLAG, 0x06, // 2 bytes, flag data
    0x14, ESP_BLE_AD_TYPE_NAME_CMPL, // 20 bytes, device name
    'p', 'i', 'g', ' ', 'd', 'e', 't', 'e', 'c', 't', 'o', 'r', ' ', 'm', 'i', 'd', 'd', 'l', 'e',
    0x05, ESP_BLE_AD_TYPE_INT_RANGE, 0x20, 0x00, 0x40, 0x00, // 5 bytes connection interval
};

static uint8_t rsp_data_raw[] = {
    0x11, ESP_BLE_AD_TYPE_128SRV_PART,
    0x0f, 0x44, 0xa3, 0xc3, 0x6f, 0x7f,
    0x3e, 0xb4, 0xc0, 0x4e, 0x71, 0xcb,
    0x24, 0x4f, 0x0b, 0xdf,
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};


void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    gatts_simple_init();
    ESP_ERROR_CHECK(gatts_simple_create_app(&app0_hdl, &app0_def));
    //ESP_ERROR_CHECK(gatts_simple_create_app(&app1_hdl, &app1_def));
    //ESP_ERROR_CHECK(gatts_simple_advertise(&adv_params, &adv_data, &rsp_data, "pig detector middle"));
    ESP_ERROR_CHECK(gatts_simple_advertise_raw(&adv_params, adv_data_raw, sizeof(adv_data_raw), rsp_data_raw, sizeof(rsp_data_raw)));
    ESP_ERROR_CHECK(gatts_simple_add_service(&app0_srv0_hdl, app0_hdl, app0_serv0_str));
    //ESP_ERROR_CHECK(gatts_simple_add_service(&app0_srv1_hdl, app0_hdl, app0_serv1_str));
    const char* string0 = "Hello world, I am string 0";
    const char* string1 = "Hello world, I am string 1";
    // sprintf((char*)app0_serv0_char0_val.attr_value, string0);
    // app0_serv0_char0_val.attr_len = strlen(string0);
    // sprintf((char*)app0_serv0_char1_val.attr_value, string1);
    // app0_serv0_char1_val.attr_len = strlen(string1);
    ESP_ERROR_CHECK(gatts_simple_add_char(&app0_srv0_char0_hdl, app0_srv0_hdl, 
            app0_serv0_char0_str, app0_serv0_char0_perm, app0_serv0_char0_prop,
            &app0_serv0_char0_val, &app0_serv0_char0_ctrl));
    // ESP_ERROR_CHECK(gatts_simple_add_char(&app0_srv0_char1_hdl, app0_srv0_hdl, 
    //         app0_serv0_char1_str, app0_serv0_char1_perm, app0_serv0_char1_prop,
    //         &app0_serv0_char1_val, NULL/*&app0_serv0_char1_ctrl*/));
    ESP_LOGI("main", "Hello World!");
}
