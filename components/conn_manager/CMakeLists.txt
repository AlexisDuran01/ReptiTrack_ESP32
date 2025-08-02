idf_component_register(
    SRCS "src/conn_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt_cliente rtc_time network_utils sensors firestore actuators
)
