#ifndef ACTUATORS_MANAGER_H
#define ACTUATORS_MANAGER_H

#include <stdbool.h>
#include "actuator.h"

// ------------------------------------------
// Manager para actuadores (actuators_manager)
// ------------------------------------------
// Este módulo se encarga de registrar, inicializar y gestionar
// múltiples actuadores que están controlados mediante base de datos,
// como Firestore.
//
// Cada actuador implementa su propia lógica de sincronización,
// lectura y actualización.
// El manager solo mantiene el registro y facilita el acceso.
//
// ------------------------------------------

// Inicializa el gestor de actuadores.
// Debe llamarse una vez al iniciar la aplicación para preparar el sistema.
void actuators_manager_init(void);

// Registra un actuador en el gestor.
// Recibe un puntero a un actuador previamente creado.
// Retorna true si el registro fue exitoso, false si no (ej. sin espacio o no inicializado).
bool actuators_manager_register(Actuator* actuator);

/**
 * @brief Crea y registra todos los actuadores disponibles en el sistema.
 * 
 * Esta función es responsable de crear los actuadores específicos que el sistema necesita,
 * y registrarlos mediante `actuators_manager_register`.
 * 
 * Retorna true si al menos un actuador fue registrado correctamente,
 * o false si hubo un error o no se registró ninguno.
 */
bool actuators_manager_register_all(void);

/**
 * @brief Inicia la lógica (tareas, sincronización, etc.) de todos los actuadores registrados.
 * 
 * Esta función debe llamarse después de registrar e inicializar todos los actuadores.
 * Lanza las tareas o procesos necesarios para que cada actuador empiece a operar.
 */
void actuators_manager_start_all(void);

// Devuelve un puntero al actuador registrado con el nombre indicado.
// Si no se encuentra un actuador con ese nombre, retorna NULL.
Actuator* actuators_manager_get_actuator_by_name(const char* name);

// Indica si el gestor de actuadores ha sido inicializado.
// Retorna true si se ha llamado a actuators_manager_init(), false si no.
bool actuators_manager_is_initialized(void);

// Libera recursos y limpia el gestor de actuadores.
// Detiene tareas si es necesario y libera memoria si los actuadores fueron creados dinámicamente.
void actuators_manager_deinit(void);

#endif // ACTUATORS_MANAGER_H
