idf_component_register(
    SRCS "src/mqtt_cliente.c" 
    INCLUDE_DIRS "include" 
    REQUIRES mqtt nvs_utils rtc_time sensors
)