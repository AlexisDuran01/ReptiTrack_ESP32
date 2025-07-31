#include "dht_sensor.h"
#include <stdlib.h>
#include <stdio.h>
#include "driver/gpio.h"
#include <dht.h>   // Incluimos para usar dht_read_float_data()
#include "mqtt_cliente.h" // Asegúrate de incluir el archivo con la función nueva
#include "esp_log.h"

static const char* TAG = "DHT11";


static bool dht_sensor_init(Sensor* self) {
    if (!self) return false;

    DHTSensorData* data = (DHTSensorData*)self->internal_data;
    if (!data) return false;

    data->sensor_type = DHT_TYPE_DHT11;  // Tipo fijo
    data->last_temp = 0;
    data->last_humidity = 0;

    return true;
}

static void* dht_sensor_read(Sensor* self) {
    if (!self) return NULL;

    DHTSensorData* data = (DHTSensorData*)self->internal_data;
    if (!data) return NULL;

    esp_err_t res = dht_read_float_data(
        data->sensor_type,
        data->gpio_num,
        &data->last_humidity,
        &data->last_temp
    );

    if (res != ESP_OK) {
		  // Reiniciamos los valores a -1 para indicar fallo
	    data->last_temp = -1;
	    data->last_humidity = -1;
        ESP_LOGE(TAG, "Error leyendo DHT11 '%s' en GPIO %d: %d \n", self->name, data->gpio_num, res);
        return NULL;
    }

    return data;
}

#include "esp_log.h"


static bool dht_sensor_publish(Sensor* self) {
    if (!self) return false;

    DHTSensorData* data = (DHTSensorData*)self->internal_data;
    if (!data) return false;

    ESP_LOGI(TAG, "Datos a enviar: %s - Temp: %.2f C, Hum: %.2f %%", self->name, data->last_temp, data->last_humidity);

    // Crear el JSON con los datos
    //Obtenemos los ultimos valores registrados, osea no volvemos a tomar la lectura
    char json_payload[128];
    snprintf(json_payload, sizeof(json_payload),
             "{\"humedad\": %.2f, \"temperatura\": %.2f}",
             data->last_humidity, data->last_temp);

    // Construir el subtopic usando el nombre del sensor
    char subtopic[64];
    snprintf(subtopic, sizeof(subtopic), "sensores/%s", self->name);

    // Publicar el JSON al topic correspondiente
    bool published = mqtt_cliente_publish_with_base(subtopic, json_payload, 1, false);
    if (published) {
        ESP_LOGI(TAG, "Publicacion exitosa para sensor %s", self->name);
    } else {
        ESP_LOGE(TAG, "Fallo en la publicacion para sensor %s", self->name);
    }

    return published;
}


Sensor* dht_sensor_create(const char* name, int gpio_num, dht_sensor_type_t sensor_type) {
    Sensor* sensor = malloc(sizeof(Sensor));
    if (!sensor) return NULL;

    DHTSensorData* data = malloc(sizeof(DHTSensorData));
    if (!data) {
        free(sensor);
        return NULL;
    }

    data->gpio_num = gpio_num;
    data->sensor_type = sensor_type;
    data->last_temp = 0;
    data->last_humidity = 0;

    sensor->name = name;
    sensor->init = dht_sensor_init;
    sensor->read = dht_sensor_read;
    sensor->publish = dht_sensor_publish;
    sensor->internal_data = data;

    return sensor;
}
