#ifndef DHT11_H
#define DHT11_H

#include "sensor.h"
#include <dht.h>   // Incluimos para usar dht_read_float_data()

// Datos específicos para DHT11
typedef struct {
    int gpio_num;       // Pin GPIO donde está conectado el sensor DHT11
    dht_sensor_type_t sensor_type;  // Tipo de sensor (DHT11, DHT22, etc)
    float last_temp;    // Última temperatura leída
    float last_humidity;// Última humedad leída
} DHTSensorData;

// Función para crear un sensor DHT
// name: identificador único (ej. "dth01")
// gpio_num: pin GPIO donde está conectado
// sensor_type: tipo del sensor (DHT_TYPE_DHT11, DHT_TYPE_AM2301, etc)
Sensor* dht_sensor_create(const char* name, int gpio_num, dht_sensor_type_t sensor_type);

#endif // DHT11_H
