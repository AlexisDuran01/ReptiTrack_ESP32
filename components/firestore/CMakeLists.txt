idf_component_register(
    SRCS "src/firestore.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_utils esp_http_client mbedtls json
)
