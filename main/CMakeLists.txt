idf_component_register(SRCS "app_main.c" "dashboard_board.c" "dashboard_call.c" "dashboard_ha.c" "dashboard_ha_web.c" "dashboard_ha_live.c" "dashboard_ui.c" "dashboard_wifi.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_timer esp_system esp_lcd esp_driver_i2c
                       PRIV_REQUIRES lvgl esp_wifi esp_event esp_netif nvs_flash esp_http_client esp_http_server esp_websocket_client json)
