idf_component_register(
    SRCS "src/rtc_time.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client json nvs_utils
)
