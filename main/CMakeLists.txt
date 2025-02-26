idf_component_register(SRCS "idf_ble_simple.c"
                    INCLUDE_DIRS "."
                    REQUIRES gatts_simple nvs_flash)
