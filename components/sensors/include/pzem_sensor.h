#ifndef PZEM_SENSOR_H
#define PZEM_SENSOR_H

#include "sensor.h"
#include "pzem004tv3.h" // Incluye la librería del PZEM

typedef struct {
    pzem_setup_t config;
    _current_values_t last_values;
} PZEMSensorData;

static bool pzem_init(Sensor* self);

// Crea un sensor PZEM
Sensor* pzem_sensor_create(const char* name, pzem_setup_t* config);

#endif // PZEM_SENSOR_H