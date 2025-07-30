idf_component_register(
	SRCS "src/network_utils.c"
	INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp_wifi
)