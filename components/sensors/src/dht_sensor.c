#include "dht_sensor.h"
#include <stdlib.h>
#include <stdio.h>
#include "driver/gpio.h"
#include <dht.h>   // Incluimos para usar dht_read_float_data()
#include "mqtt_cliente.h" // Asegúrate de incluir el archivo con la función nueva
#include "esp_log.h"

static const char* TAG = "DHT11";


// --- Inicialización del sensor (llamada internamente por manager) ---
static bool dht_sensor_init(Sensor* self) {
    if (!self) return false;  // Verifica puntero válido
    DHTSensorData* data = (DHTSensorData*)self->internal_data;  // Accede a datos internos
    if (!data) return false;  // Verifica memoria interna

    data->last_temp = 0;      // Inicializa temperatura
    data->last_humidity = 0;  // Inicializa humedad
    return true;              // Indica que init fue exitosa
}

// Funcion para leer los datos del sensor DHT y guardarlos internamente
static void* dht_sensor_read(Sensor* self) {
    // Verifica que el puntero al sensor no sea NULL
    if (!self) return NULL;

    // Accede a los datos internos del sensor (estructura DHTSensorData)
    DHTSensorData* data = (DHTSensorData*)self->internal_data;
    // Verifica que los datos internos existan
    if (!data) return NULL;

    // Llama a la funcion de la libreria que lee temperatura y humedad
    // Los valores leidos se almacenan directamente en la estructura interna
    esp_err_t res = dht_read_float_data(
        data->sensor_type,     // Tipo de sensor (DHT11, DHT22, etc.)
        data->gpio_num,        // Pin GPIO donde está conectado
        &data->last_humidity,  // Referencia donde guardar la humedad
        &data->last_temp       // Referencia donde guardar la temperatura
    );

    // Si ocurre un error durante la lectura
    if (res != ESP_OK) {
        // Se asigna -1 a los valores para indicar que la lectura falló
        data->last_temp = -1;
        data->last_humidity = -1;

        // Se imprime un mensaje de error con el nombre y pin del sensor
        ESP_LOGE(TAG, "Error leyendo DHT11 '%s' en GPIO %d: %d \n", self->name, data->gpio_num, res);
        return NULL;  // Retorna NULL indicando que la lectura falló
    }

    // Si la lectura fue exitosa, retorna el puntero a los datos internos
    return data;
}



// Funcion que publica los datos del sensor DHT a traves de MQTT
static bool dht_sensor_publish(Sensor* self) {
    // Verifica que el puntero al sensor no sea NULL
    if (!self) return false;

    // Accede a los datos internos del sensor (estructura DHTSensorData)
    DHTSensorData* data = (DHTSensorData*)self->internal_data;
    if (!data) return false;

    // Imprime en log los datos que se van a enviar
  //  ESP_LOGI(TAG, "Datos a enviar: %s - Temp: %.2f C, Hum: %.2f %%", self->name, data->last_temp, data->last_humidity);

    // Creamos el payload JSON que se enviará por MQTT
    // IMPORTANTE: usamos los ultimos valores leidos y guardados en memoria
    // Se accede directamente a los campos guardados en internal data
    // No se vuelve a hacer lectura del sensor aqui, simplemente se recupeara
    char json_payload[128];
    snprintf(json_payload, sizeof(json_payload),
             "{\"humedad\": %.2f, \"temperatura\": %.2f}",
             data->last_humidity, data->last_temp);

    // Construimos el subtopic MQTT usando el nombre del sensor
    // Ejemplo: sensores/dht01
    char subtopic[64];
    snprintf(subtopic, sizeof(subtopic), "sensores/%s", self->name);

    // Publicamos el mensaje JSON al topic MQTT
    bool published = mqtt_cliente_publish_with_base(subtopic, json_payload, 1, false);
    
    // Verificamos si la publicacion fue exitosa y lo registramos en el log
    if (published) {
       // ESP_LOGI(TAG, "Publicacion exitosa para sensor %s", self->name);
    } else {
        ESP_LOGE(TAG, "Fallo en la publicacion para sensor %s", self->name);
    }

    // Retorna true si se publico correctamente, false en caso contrario
    return published;
}



// Crea e inicializa un nuevo objeto Sensor para un sensor DHT (DHT11, DHT22, etc.)
Sensor* dht_sensor_create(const char* name, int gpio_num, dht_sensor_type_t sensor_type) {
    // Reservamos memoria para la estructura base Sensor
    Sensor* sensor = malloc(sizeof(Sensor));
    if (!sensor) return NULL;  // Si falla la reserva, retornamos NULL

    // Reservamos memoria para los datos específicos del sensor DHT
    DHTSensorData* data = malloc(sizeof(DHTSensorData));
    if (!data) {
        free(sensor);          // Liberamos la memoria de sensor si falla la de data
        return NULL;
    }

    // Inicializamos los campos de configuración del sensor DHT
    data->gpio_num = gpio_num;           // GPIO en el que está conectado el sensor
    data->sensor_type = sensor_type;     // Tipo de sensor (DHT11, DHT22, etc.)
    data->last_temp = 0;                 // Inicializamos temperatura en 0
    data->last_humidity = 0;             // Inicializamos humedad en 0

    // Configuramos la estructura base del sensor
    sensor->name = name;                 // Asignamos el nombre del sensor
    sensor->init = dht_sensor_init;      // Función para inicializar
    sensor->read = dht_sensor_read;      // Función para leer datos del sensor
    sensor->publish = dht_sensor_publish;// Función para publicar datos vía MQTT
    sensor->internal_data = data;        // Asignamos los datos internos (específicos del sensor)

    // Retornamos el sensor ya creado y configurado
    return sensor;
}

