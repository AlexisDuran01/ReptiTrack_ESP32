#include "sensors_manager.h"     // Declaraciones del manager de sensores
#include "sensor.h"              // Interfaz base común para todos los sensores
#include "dht_sensor.h"               // Implementación del sensor DHT11 (ejemplo)
#include <stdlib.h>              // Para uso de NULL, malloc, etc.
#include <stdio.h>               // Para printf si se desea debug
#include <string.h>              // Para strcmp en búsqueda de sensores
#include <freertos/FreeRTOS.h>   // Soporte para FreeRTOS
#include <freertos/task.h>       // API para crear tareas
#include "esp_log.h"             // Log para ESP-IDF

// -------------------------------------------------------------------
// Definiciones internas
// -------------------------------------------------------------------

#define MAX_SENSORS 20  ///< Máximo número de sensores permitidos

static Sensor* sensors[MAX_SENSORS];   ///< Arreglo de sensores registrados
static int sensor_count = 0;           ///< Número de sensores registrados actualmente

static bool sensors_initialized = false;  ///< Bandera para saber si ya fue inicializado

// Handles de tareas (para evitar duplicados)
static TaskHandle_t read_task_handle = NULL;
static TaskHandle_t publish_task_handle = NULL;

// Intervalos de ejecución (en ticks)
static const TickType_t READ_DELAY = pdMS_TO_TICKS(2000);     // Cada 2s
static const TickType_t PUBLISH_DELAY = pdMS_TO_TICKS(3000);  // Cada 3s

static const char* TAG = "SensorsManager";  ///< Etiqueta para logs

// -------------------------------------------------------------------
// Función interna: sensors_manager_register
// Registra un sensor en el arreglo si hay espacio disponible.
// -------------------------------------------------------------------
bool sensors_manager_register(Sensor* sensor) {
    if (!sensor || sensor_count >= MAX_SENSORS) {
        ESP_LOGW(TAG, "No se pudo registrar sensor (nulo o sin espacio)");
        return false;
    }

    sensors[sensor_count++] = sensor;
    ESP_LOGI(TAG, "Sensor registrado: %s", sensor->name);
    return true;
}

// -------------------------------------------------------------------
// Función interna: sensors_manager_register_all
// Crea y registra todos los sensores definidos en el sistema.
// -------------------------------------------------------------------
void sensors_manager_register_all(void) {
	
    // Ejemplo: Crear y registrar un sensor DHT11 en GPIO 4
    
	/*
	 * DHT_TYPE_DHT11 se puede usar porque dht_sensor.h incluye dht.h,
	 * por lo que los simbolos de dht.h quedan visibles indirectamente.
	 * Esto se llama inclusion transitiva: una cabecera expone definiciones
	 * de otra que incluye, sin necesidad de incluirla directamente.
	 */

    Sensor* dht11 = dht_sensor_create("dht01", 4, DHT_TYPE_DHT11);
    if (dht11) {
        sensors_manager_register(dht11);
    } else {
        ESP_LOGE(TAG, "No se pudo crear el sensor DHT11");
    }



    // Aquí puedes agregar otros sensores:
    // Sensor* ldr = ldr_create("ldr01", 5);
    // sensors_manager_register(ldr);
}

// -------------------------------------------------------------------
// Función pública: sensors_manager_init
// Inicializa el sistema y registra sensores.
// -------------------------------------------------------------------
void sensors_manager_init(void) {
	
    if (sensors_initialized) {
        ESP_LOGW(TAG, "Gestor de sensores ya fue inicializado");
        return;
    }

    ESP_LOGI(TAG, "Inicializando el gestor de sensores...");

    sensor_count = 0;
    sensors_manager_register_all();
    sensors_initialized = true;

    ESP_LOGI(TAG, "Inicializacion completada. %d sensores registrados", sensor_count);
}


// -------------------------------------------------------------------
// Función interna: sensors_read_task
// Tarea que recorre y lee todos los sensores en bucle.
// -------------------------------------------------------------------
static void sensors_read_task(void* arg) {
    while (1) {
        for (int i = 0; i < sensor_count; i++) {
            if (sensors[i] && sensors[i]->read) {
                sensors[i]->read(sensors[i]);
            }
        }
        vTaskDelay(READ_DELAY);
    }
}

// -------------------------------------------------------------------
// Función interna: sensors_publish_task
// Tarea que recorre y publica los datos de los sensores.
// -------------------------------------------------------------------
static void sensors_publish_task(void* arg) {
    while (1) {
        for (int i = 0; i < sensor_count; i++) {
            if (sensors[i] && sensors[i]->publish) {
                sensors[i]->publish(sensors[i]);
            }
        }
        vTaskDelay(PUBLISH_DELAY);
    }
}


// -------------------------------------------------------------------
// Función pública: sensors_manager_start_read
// Lanza la tarea FreeRTOS que lee los sensores periódicamente.
// -------------------------------------------------------------------
void sensors_manager_start_read(void) {
    if (read_task_handle != NULL) return;

    xTaskCreate(
        sensors_read_task,
        "SensorsRead",
        4096,
        NULL,
        5,
        &read_task_handle
    );
}

// -------------------------------------------------------------------
// Función pública: sensors_manager_start_publish
// Lanza la tarea FreeRTOS que publica los datos periódicamente.
// -------------------------------------------------------------------
void sensors_manager_start_publish(void) {
    if (publish_task_handle != NULL) return;

    xTaskCreate(
        sensors_publish_task,
        "SensorsPublish",
        4096,
        NULL,
        5,
        &publish_task_handle
    );
}



// -------------------------------------------------------------------
// Función pública: sensors_manager_read_all
// Ejecuta una lectura inmediata de todos los sensores.
// -------------------------------------------------------------------
void sensors_manager_read_all(void) {
    for (int i = 0; i < sensor_count; i++) {
        if (sensors[i] && sensors[i]->read) {
            sensors[i]->read(sensors[i]);
        }
    }
}

// -------------------------------------------------------------------
// Función pública: sensors_manager_publish_all
// Publica datos inmediatamente de todos los sensores que lo permitan
// -------------------------------------------------------------------
void sensors_manager_publish_all(void) {
    for (int i = 0; i < sensor_count; i++) {
        if (sensors[i] && sensors[i]->publish) {
            sensors[i]->publish(sensors[i]);
        }
    }
}

// -------------------------------------------------------------------
// Función pública: sensors_manager_get_sensor_by_name
// Busca un sensor registrado por nombre.
// -------------------------------------------------------------------
Sensor* sensors_manager_get_sensor_by_name(const char* name) {
    if (!name) return NULL;

    for (int i = 0; i < sensor_count; i++) {
        if (sensors[i] && strcmp(sensors[i]->name, name) == 0) {
            return sensors[i];
        }
    }

    return NULL;
}

// -------------------------------------------------------------------
// Función pública: sensors_manager_is_initialized
// Retorna si el sistema fue inicializado.
// -------------------------------------------------------------------
bool sensors_manager_is_initialized(void) {
    return sensors_initialized;
}

// -------------------------------------------------------------------
// Función pública: sensors_manager_is_fully_operational
// Verifica si el sistema está totalmente listo.
// -------------------------------------------------------------------
bool sensors_manager_is_fully_operational(void) {
    return sensors_initialized && read_task_handle != NULL && publish_task_handle != NULL;
}

// -------------------------------------------------------------------
// Función pública: sensors_manager_deinit
// Detiene tareas y borra estado del sistema de sensores.
// -------------------------------------------------------------------
void sensors_manager_deinit(void) {
    if (read_task_handle) {
        vTaskDelete(read_task_handle);
        read_task_handle = NULL;
    }

    if (publish_task_handle) {
        vTaskDelete(publish_task_handle);
        publish_task_handle = NULL;
    }

    for (int i = 0; i < sensor_count; i++) {
        free(sensors[i]);  // Solo si fueron creados con malloc. Si usas static, omite esto.
    }

    sensor_count = 0;
    sensors_initialized = false;

    ESP_LOGI(TAG, "Gestor de sensores detenido y limpiado.");
}
