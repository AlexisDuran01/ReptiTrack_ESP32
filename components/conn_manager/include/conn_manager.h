#pragma once

/*es una directiva que incluye el encabezado estándar en C para trabajar con el 
tipo de dato booleano

Define el tipo bool que solo puede tener dos valores: true o false
*/
#include <stdbool.h>

/// @brief Obtiene la instancia singleton del gestor de conectividad.
/// @return Puntero genérico a la instancia única de conn_manager.
void *conn_manager_get_instance(void);

/// @brief Inicializa el gestor de conectividad.
/// Se encarga de preparar los recursos internos, variables y configurar servicios.
/// Debe llamarse antes de usar otras funciones del gestor.
void conn_manager_init(void);


/// @brief Detiene el gestor de conectividad y libera recursos asignados.
/// Por ejemplo, puede detener tareas, liberar memoria, desconectar clientes, etc.
void conn_manager_deinit(void);

