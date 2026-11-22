idf_component_register(SRCS  "eez_ui/ui.c" "eez_ui/screens.c" "eez_ui/styles.c" "eez_ui/images.c" "eez_ui/ui_font_roboto.c" "eez_ui/ui_image_icon_settings.c" "eez_ui/dashboard_eez_actions.c" "app_main.c" "dashboard_board.c" "dashboard_call.c" "dashboard_ha.c" "dashboard_ha_web.c" "dashboard_ha_live.c"  "dashboard_wifi.c"
                       INCLUDE_DIRS "." "eez_ui"
                       REQUIRES esp_timer esp_system esp_lcd esp_driver_i2c
                       PRIV_REQUIRES esp_app_format lvgl esp_wifi esp_event esp_netif nvs_flash esp_http_client esp_http_server esp_websocket_client cjson)
