idf_component_register(
    SRCS "src/dht_sensor.c" "src/sensors_manager.c" "src/pzem_sensor.c"
    INCLUDE_DIRS "include"
    REQUIRES dht mqtt_cliente nvs_utils pzem004tv3
)