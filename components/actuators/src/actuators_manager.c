#include "actuators_manager.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

#include "dispenser.h"  // Incluir aquí los actuadores que vas a registrar

#define MAX_ACTUATORS 10
#define TAG "ACTUATORS_MANAGER"

static Actuator* actuators[MAX_ACTUATORS];
static int actuator_count = 0;
static bool initialized = false;

// Inicializa el gestor de actuadores
void actuators_manager_init(void) {
    if (initialized) {
        ESP_LOGW(TAG, "Gestor de actuadores ya estaba inicializado");
        return;
    }

    memset(actuators, 0, sizeof(actuators));
    actuator_count = 0;

    initialized = true;  // <-- Importante activar antes de registrar

    if (!actuators_manager_register_all()) {
        ESP_LOGW(TAG, "No se registraron actuadores");
    }

    ESP_LOGI(TAG, "Gestor de actuadores inicializado");
}

// Registra un actuador si hay espacio y no está duplicado
bool actuators_manager_register(Actuator* actuator) {
    if (!initialized) {
        ESP_LOGE(TAG, "Gestor no inicializado");
        return false;
    }

    if (!actuator || !actuator->name) {
        ESP_LOGE(TAG, "Actuador inválido (puntero nulo o sin nombre)");
        return false;
    }

    if (actuator_count >= MAX_ACTUATORS) {
        ESP_LOGW(TAG, "Límite máximo de actuadores alcanzado");
        return false;
    }

    for (int i = 0; i < actuator_count; ++i) {
        if (strcmp(actuators[i]->name, actuator->name) == 0) {
            ESP_LOGW(TAG, "Actuador duplicado: %s", actuator->name);
            return false;
        }
    }

    actuators[actuator_count++] = actuator;
    ESP_LOGI(TAG, "Actuador registrado: %s", actuator->name);
    return true;
}

// Crea y registra todos los actuadores disponibles
bool actuators_manager_register_all(void) {
    bool alguno_registrado = false;

    // Crear y registrar dispenser (ejemplo)
    Actuator* dispenser = dispenser_create(33, 25, 32, 18);
    if (dispenser && actuators_manager_register(dispenser)) {
        alguno_registrado = true;
    }

    return alguno_registrado;
}

// Inicializa e inicia todos los actuadores registrados
void actuators_manager_start_all(void) {
    if (!initialized) {
        ESP_LOGE(TAG, "Gestor no inicializado");
        return;
    }

    for (int i = 0; i < actuator_count; ++i) {
        Actuator* a = actuators[i];

        if (a->init) {
            if (!a->init(a)) {
                ESP_LOGE(TAG, "Error en init() de actuador: %s", a->name);
                continue;
            }
        }

        if (a->start) {
            ESP_LOGI(TAG, "Iniciando actuador: %s", a->name);
            a->start(a);
        }
    }
}

// Devuelve actuador por nombre o NULL si no existe
Actuator* actuators_manager_get_actuator_by_name(const char* name) {
    if (!initialized || !name) return NULL;

    for (int i = 0; i < actuator_count; ++i) {
        if (strcmp(actuators[i]->name, name) == 0) {
            return actuators[i];
        }
    }

    return NULL;
}

// Retorna si el gestor fue inicializado
bool actuators_manager_is_initialized(void) {
    return initialized;
}

// Libera recursos y detiene actuadores
void actuators_manager_deinit(void) {
    if (!initialized) return;

    for (int i = 0; i < actuator_count; ++i) {
        Actuator* a = actuators[i];

        if (a->deinit) {
            a->deinit(a);
        }

        if (a->internal_data) {
            free(a->internal_data);
        }

        free(a);
    }

    actuator_count = 0;
    initialized = false;
    ESP_LOGI(TAG, "Gestor de actuadores liberado");
}
