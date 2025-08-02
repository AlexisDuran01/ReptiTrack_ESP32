idf_component_register(
    SRCS  "src/actuators_manager.c" "src/dispenser.c"
    INCLUDE_DIRS "include"
    REQUIRES dht mqtt_cliente nvs_utils json rtc_time
)