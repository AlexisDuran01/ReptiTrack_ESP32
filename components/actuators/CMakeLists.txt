idf_component_register(
    SRCS  
        "src/actuators_manager.c"
        "src/dispenser/dispenser.c"
        "src/dispenser/dispenser_motor.c"
        "src/dispenser/dispenser_firestore.c"
        "src/dispenser/dispenser_utils.c"
        "src/dispenser/dispenser_led.c"
    INCLUDE_DIRS 
        "include"
        "include/dispenser"
    REQUIRES dht mqtt_cliente nvs_utils json rtc_time firestore
)