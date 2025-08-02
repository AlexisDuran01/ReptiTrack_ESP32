#ifndef ACTUATOR_H
#define ACTUATOR_H

#include <stdbool.h>

/**
 * Estructura base para representar un actuador controlado mediante base de datos (Firestore).
 * 
 * La lógica de activación o desactivación del actuador depende de los datos obtenidos o enviados
 * mediante funciones de sincronización (`fetch` y `update`).
 */

typedef struct Actuator Actuator;

/**
 * @brief Función de inicialización del actuador (configura GPIO, estructuras internas, etc).
 */
typedef bool (*actuator_init_t)(Actuator* self);

typedef void (*actuator_start_t)(Actuator* self);

/**
 * @brief Función que obtiene el estado del actuador desde Firestore.
 */
typedef bool (*actuator_fetch_t)(Actuator* self);

/**
 * @brief Función que actualiza el estado del actuador en Firestore.
 */
typedef bool (*actuator_update_t)(Actuator* self);

typedef bool (*actuator_deinit_t)(Actuator* self);

/**
 * @brief Estructura base que representa un actuador.
 */
struct Actuator {
    const char* name;                    ///< Identificador único del actuador (ej. "rele01")

    actuator_init_t init;               ///< Función para inicializar el actuador
    actuator_start_t start;  // <--- agregar aquí si quieres un start
    actuator_fetch_t fetch_from_db;     ///< Obtiene el estado desde la base de datos
    actuator_update_t update_to_db;     ///< Actualiza el estado en la base de datos
    actuator_deinit_t deinit;    // <- Agregar aquí


    void* internal_data;                ///< Datos específicos del actuador (ej. pin GPIO, estado actual, path Firestore, etc.)
};

#endif // ACTUATOR_H
