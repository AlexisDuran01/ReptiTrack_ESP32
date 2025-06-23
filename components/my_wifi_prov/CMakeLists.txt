idf_component_register(
  SRCS "src/app_prov.c" "src/ble_scheme.c" "src/sec2_params.c"
  INCLUDE_DIRS "include" 
  REQUIRES
    esp_wifi
    nvs_flash
    esp_event
    esp_netif
    esp_common
    wifi_provisioning
    driver
)