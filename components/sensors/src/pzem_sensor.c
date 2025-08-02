#include "pzem_sensor.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "mqtt_cliente.h" // Asegúrate de incluir el archivo con la función nueva


static const char* TAG = "PZEMSensor";

// Inicialización usando la función de la librería
static bool pzem_init(Sensor* self) {
    if (!self || !self->internal_data) return false;
    PZEMSensorData* data = (PZEMSensorData*)self->internal_data;
    PzemInit(&data->config); // Esto configura el UART internamente
    return true;
}

// Función de lectura para el sensor PZEM
static void* pzem_read(Sensor* self) {
    if (!self || !self->internal_data) return NULL;
    PZEMSensorData* data = (PZEMSensorData*)self->internal_data;

    bool ok = PzemGetValues(&data->config, &data->last_values);

    if (!ok) {
        // Si ocurre un error, asigna -1 a los valores
        data->last_values.voltage   = -1;
        data->last_values.current   = -1;
        data->last_values.power     = -1;
        data->last_values.energy    = -1;
        data->last_values.frequency = -1;
        data->last_values.pf        = -1;

        ESP_LOGE(TAG, "Error leyendo PZEM '%s'", self->name);
        return NULL;
    }

    ESP_LOGI(TAG, "Vrms: %.1fV - Irms: %.3fA - P: %.1fW - E: %.2fWh", 
        data->last_values.voltage, 
        data->last_values.current, 
        data->last_values.power, 
        data->last_values.energy);

    return &data->last_values;
}


// Opcional: función de publicación
static bool pzem_publish(Sensor* self) {
    if (!self || !self->internal_data) return false;

    PZEMSensorData* data = (PZEMSensorData*)self->internal_data;

    // Prepara el payload JSON con los últimos valores leídos
    char json_payload[128];
    snprintf(json_payload, sizeof(json_payload),
        "{\"voltaje\": %.1f, \"corriente\": %.3f, \"potencia\": %.1f, \"energia\": %.2f}",
        data->last_values.voltage,
        data->last_values.current,
        data->last_values.power,
        data->last_values.energy
    );

    // Construye el subtopic MQTT usando el nombre del sensor
    char subtopic[64];
    snprintf(subtopic, sizeof(subtopic), "sensores/%s", self->name);

    // Publica el mensaje JSON al topic MQTT
    bool published = mqtt_cliente_publish_with_base(subtopic, json_payload, 1, false);

    // Opcional: log de resultado
    if (published) {
		ESP_LOGI(TAG, "Publicacion exitosa para sensor %s", self->name);
    } else {
        ESP_LOGE("PZEMSensor", "Fallo en la publicacion para sensor %s", self->name);
    }

    return published;
}

// Crea e inicializa el sensor PZEM
Sensor* pzem_sensor_create(const char* name, pzem_setup_t* config) {
    Sensor* sensor = (Sensor*)malloc(sizeof(Sensor));
    if (!sensor) return NULL;

    PZEMSensorData* data = (PZEMSensorData*)malloc(sizeof(PZEMSensorData));
    if (!data) {
        free(sensor);
        return NULL;
    }
    memcpy(&data->config, config, sizeof(pzem_setup_t));
    memset(&data->last_values, 0, sizeof(_current_values_t));

    sensor->name = strdup(name);
    sensor->init = pzem_init; // Ahora sí, inicialización con PzemInit
    sensor->read = pzem_read;
    sensor->publish = pzem_publish;
    sensor->internal_data = data;

    return sensor;
}