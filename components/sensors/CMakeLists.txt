idf_component_register(
    SRCS "src/dht_sensor.c" "src/sensors_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES dht mqtt_cliente
)