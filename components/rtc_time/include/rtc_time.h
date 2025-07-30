#ifndef RTC_TIME_H

#define RTC_TIME_H
#include <stdbool.h>  // Para usar bool
#include "esp_err.h"
#include <time.h>



/**
 * @brief Sincroniza la hora local del ESP32 consultando un servidor de hora en línea (WorldTimeAPI).
 * 
 * Esta función realiza una petición HTTP GET a la API pública de WorldTimeAPI usando la zona horaria
 * proporcionada (por ejemplo, "America/Mexico_City"). Luego analiza la respuesta JSON y actualiza
 * el reloj interno (RTC) del ESP32 con la hora recibida.
 * 
 * Ejemplo de uso:
 *     rtc_time_sync_with_timezone("America/Mexico_City");
 *
 * @param timezone Una cadena con el nombre de la zona horaria en formato "Área/Ciudad".
 *                 Ejemplo válido: "America/Mexico_City".
 * 
 * @return ESP_OK si la sincronización fue exitosa.
 *         ESP_ERR_INVALID_ARG si el parámetro es inválido.
 *         ESP_FAIL o códigos relacionados si hay problemas con la red o el servidor.
 */
esp_err_t rtc_time_sync_with_timezone(const char *timezone);

/**
 * @brief Imprime la hora actual del RTC del ESP32 en formato legible para humanos.
 * 
 * Esta función obtiene la hora local actual (si ya fue previamente sincronizada)
 * y la imprime por consola (usualmente vía `ESP_LOGI`) en formato:
 *     YYYY-MM-DD HH:MM:SS
 *
 * Ejemplo de salida:
 *     2025-07-26 14:00:00
 *
 * @return ESP_OK si la hora fue obtenida e impresa correctamente.
 *         ESP_ERR_INVALID_STATE si el RTC aún no ha sido sincronizado.
 */
esp_err_t rtc_time_print_current(void);

/**
 * @brief Devuelve la hora actual del sistema en formato `time_t` (segundos desde 1970).
 * 
 * Esta función es útil si quieres realizar cálculos, comparar fechas o almacenar
 * marcas de tiempo (timestamps) para registros o sensores.
 *
 * @param out_time Puntero a una variable `time_t` donde se almacenará el resultado.
 *                 Este puntero debe ser válido y no nulo.
 * 
 * @return ESP_OK si se obtuvo la hora correctamente.
 *         ESP_ERR_INVALID_ARG si el puntero es nulo.
 *         ESP_ERR_INVALID_STATE si la hora aún no ha sido sincronizada.
 */
esp_err_t rtc_time_get_current(time_t *out_time);

/**
 * @brief Convierte una cadena de fecha ISO 8601 (ej. "2025-07-26T14:15:30Z") a formato time_t.
 * 
 * Esta función es útil para convertir fechas recibidas desde Firestore o APIs que usan ISO 8601.
 * 
 * @param iso_str Cadena de fecha en formato ISO 8601 (UTC, terminada en 'Z').
 * @param out_time Puntero donde se almacenará el resultado como time_t.
 * 
 * @return ESP_OK si fue convertido correctamente.
 *         ESP_ERR_INVALID_ARG si el formato es incorrecto o punteros inválidos.
 */
esp_err_t rtc_time_parse_iso8601_string(const char *iso_str, time_t *out_time);

/**
 * @brief Consulta si el flag de sincronización de hora está activo en NVS.
 * 
 * Esta función permite saber si el sistema ya sincronizó la hora correctamente previamente.
 * 
 * @param[out] out_synced Puntero a bool donde se almacenará el resultado.
 *                        true = sincronizado, false = no sincronizado o no encontrado.
 * 
 * @return ESP_OK si se pudo consultar correctamente.
 *         Otro código de error en caso de fallo en lectura NVS.
 */
esp_err_t rtc_time_is_synchronized(bool *out_synced);

/**
 * @brief Establece el flag de sincronización de hora en NVS.
 * 
 * Debe llamarse cuando se haya sincronizado la hora correctamente para guardar el estado.
 * 
 * @param synced true para marcar sincronizado, false para limpiar el flag.
 * 
 * @return ESP_OK si se pudo guardar correctamente.
 *         Otro código de error en caso de fallo en escritura NVS.
 */
esp_err_t rtc_time_set_synchronized(bool synced);

/**
 * @brief Borra el flag de sincronización de hora en NVS.
 * 
 * Útil para resetear el estado, indicando que no está sincronizado.
 * 
 * @return ESP_OK si se pudo borrar correctamente.
 *         Otro código de error en caso de fallo.
 */
esp_err_t rtc_time_clear_synchronized(void);



#endif // RTC_TIME_H
