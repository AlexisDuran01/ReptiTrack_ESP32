#ifndef SENSORS_MANAGER_H  // Si SENSORS_MANAGER_H no está definido
#define SENSORS_MANAGER_H  // Defínelo ahora para marcar que ya se incluyó este archivo

#include <stdbool.h>
#include "sensor.h"

// ----------------------------
// Previene que este archivo de cabecera (.h) 
// sea incluido más de una vez en un mismo archivo de compilación.
// Esto evita redefiniciones y errores de compilación.
// ----------------------------

// ===========================================================
// FUNCIONES PÚBLICAS DEL MANAGER DE SENSORES
// ===========================================================

/**
 * @brief Inicializa el manager de sensores y registra todos los sensores disponibles.
 *
 * Esta función debe llamarse una sola vez al iniciar la aplicación.
 * Internamente, crea y registra todos los sensores mediante
 * la función `sensors_manager_register_all`.
 */
void sensors_manager_init(void);

/**
 * @brief Lanza una tarea FreeRTOS que lee todos los sensores registrados periódicamente.
 *
 * Ejecuta la función `read()` de cada sensor a intervalos fijos.
 * No crea la tarea si ya existe una corriendo.
 */
void sensors_manager_start_read(void);

/**
 * @brief Lanza una tarea FreeRTOS que publica los datos de los sensores periódicamente.
 *
 * Ejecuta la función `publish()` de cada sensor (si existe) a intervalos fijos.
 * No crea la tarea si ya existe una corriendo.
 */
void sensors_manager_start_publish(void);

/**
 * @brief Realiza una lectura inmediata de todos los sensores.
 *
 * Esta función recorre todos los sensores y ejecuta su función `read()` una vez.
 */
void sensors_manager_read_all(void);

/**
 * @brief Publica inmediatamente los datos de todos los sensores que lo permitan.
 *
 * Esta función recorre todos los sensores y ejecuta su función `publish()`, si está definida.
 */
void sensors_manager_publish_all(void);

/**
 * @brief Devuelve un puntero a un sensor específico registrado por su nombre.
 *
 * @param name Nombre del sensor (ej. "dht11_01").
 * @return Puntero al sensor si se encuentra, o NULL si no está registrado.
 */
Sensor* sensors_manager_get_sensor_by_name(const char* name);

/**
 * @brief Verifica si el sistema de sensores ya fue inicializado.
 *
 * @return true si `sensors_manager_init()` ya fue llamado, false si no.
 */
bool sensors_manager_is_initialized(void);

/**
 * @brief Verifica si el sistema está completamente operativo.
 *
 * Esto puede incluir verificar que las tareas de lectura/publicación estén corriendo.
 *
 * @return true si el sistema está listo, false si no.
 */
bool sensors_manager_is_fully_operational(void);

// ===========================================================
// FUNCIONES INTERNAS (SOLO USO DENTRO DE sensors_manager.c)
// ===========================================================

/**
 * @brief Crea y registra manualmente todos los sensores necesarios.
 *
 * Esta función debe contener las llamadas a los creadores de sensores,
 * como `dht11_create(...)`, `ldr_create(...)`, etc.
 *
 */
bool sensors_manager_register_all(void);

/**
 * @brief Libera memoria y detiene tareas relacionadas al manager.
 *
 * Útil para reinicios del sistema o pruebas.
 */
void sensors_manager_deinit(void);


/**
 * @brief  Verifica si el sistema está totalmente listo
 *
 * Verifica que se haya inicializado una instancia y esten activas las tareas
 */

bool sensors_manager_is_fully_operational(void);
#endif // Fin del include guard

