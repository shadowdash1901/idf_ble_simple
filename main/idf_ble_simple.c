#include <stdio.h>
#include "esp_log.h"
#include "gatts_simple.h"
#include "esp_check.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

gatts_simple_app_handle_t app0_hdl;
gatts_simple_app_handle_t app1_hdl;
gatts_simple_service_handle_t app0_srv0_hdl;
gatts_simple_service_handle_t app0_srv1_hdl;

gatts_simple_app_def_t app0_def = {
    .app_id = 0,
    .device_name = "app0",
};
const char* app0_serv0_str = "df0b4f24-cb71-4ec0-b43e-7f6fc3a3440f";
const char* app0_serv1_str = "0827e3bc-123a-46b7-b2dd-c7bfc02e8dcb";

gatts_simple_app_def_t app1_def = {
    .app_id = 1,
    .device_name="app1",
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
    esp_bt_uuid_t app0_serv0 = {
        .len = ESP_UUID_LEN_128,
    };
    uuid_from_str(app0_serv0.uuid.uuid128, app0_serv0_str);
    esp_bt_uuid_t app0_serv1 = {
        .len = ESP_UUID_LEN_128,
    };
    uuid_from_str(app0_serv1.uuid.uuid128, app0_serv1_str);
    ESP_ERROR_CHECK(gatts_simple_add_service(app0_hdl, &app0_srv0_hdl, &app0_serv0));
    ESP_ERROR_CHECK(gatts_simple_add_service(app0_hdl, &app0_srv1_hdl, &app0_serv1));

    ESP_ERROR_CHECK(gatts_simple_create_app(&app1_hdl, &app1_def));
    ESP_LOGI("main", "Hello World!");
}
