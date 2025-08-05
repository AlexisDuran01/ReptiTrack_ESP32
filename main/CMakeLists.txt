idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES my_wifi_prov sensors nvs_utils actuators pzem004tv3 rtc_time
)
