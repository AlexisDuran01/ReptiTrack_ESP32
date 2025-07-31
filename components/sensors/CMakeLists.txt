idf_component_register(
    SRCS "src/dht11.c" "src/sensors_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES dht mqtt_cliente
)