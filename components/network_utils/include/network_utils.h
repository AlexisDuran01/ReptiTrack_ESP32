// Asegura que este archivo solo se incluya una vez por unidad de compilación, evitando redefiniciones.
#pragma once

// Incluye soporte para el tipo de dato `bool` en C.
#include <stdbool.h>

// Incluye las definiciones de tipos y códigos de error estándar de ESP-IDF.
#include <esp_err.h>


/**
 * @brief Verifica si el dispositivo tiene acceso a Internet.
 *
 * Realiza una solicitud HTTP GET a una URL especial que responde con código 204
 * si hay conectividad a Internet.
 *
 * @param timeout_ms Tiempo máximo de espera en milisegundos para la solicitud.
 * @return true si se obtuvo una respuesta 204, lo que indica conexión a Internet.
 * @return false si no hay conexión o hay un error.
 */
bool check_internet_connection(int timeout_ms);

/**
 * @brief Verifica si el ESP32 está actualmente conectado a una red Wi-Fi.
 *
 * @return true si está conectado a un punto de acceso.
 * @return false si no está conectado.
 */
bool is_wifi_connected(void);

/**
 * @brief Obtiene la dirección IP local asignada al ESP32 (modo estación).
 *
 * @param ip_str Búfer de salida donde se escribirá la dirección IP en formato string.
 * @param max_len Longitud máxima del búfer `ip_str`.
 * @return ESP_OK si la IP fue obtenida exitosamente.
 * @return ESP_FAIL si ocurrió algún error (por ejemplo, si no hay IP asignada).
 */
esp_err_t get_local_ip(char *ip_str, size_t max_len);



/**
 * @brief Obtiene la dirección IP del gateway (puerta de enlace) asignada al ESP32.
 *
 * @param gw_str Búfer de salida donde se escribirá la dirección IP del gateway en formato string.
 * @param max_len Longitud máxima del búfer `gw_str`.
 * @return ESP_OK si la IP fue obtenida exitosamente.
 * @return ESP_FAIL si ocurrió algún error.
 */
esp_err_t get_gateway_ip(char *gw_str, size_t max_len);

/**
 * @brief Obtiene la dirección IP del servidor DNS primario asignado al ESP32.
 *
 * @param dns_str Búfer de salida donde se escribirá la dirección IP del DNS en formato string.
 * @param max_len Longitud máxima del búfer `dns_str`.
 * @return ESP_OK si la IP fue obtenida exitosamente.
 * @return ESP_FAIL si ocurrió algún error.
 */
esp_err_t get_dns_ip(char *dns_str, size_t max_len);




