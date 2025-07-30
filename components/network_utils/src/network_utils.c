// Incluye el archivo de cabecera personalizado que contiene las declaraciones de las funciones.
#include "network_utils.h"

// Incluye la biblioteca HTTP de ESP-IDF para hacer solicitudes HTTP.
#include "esp_http_client.h"

// Incluye la biblioteca de Wi-Fi de ESP-IDF para acceder a funciones relacionadas con la conexión Wi-Fi.
#include "esp_wifi.h"

// Incluye la biblioteca de interfaces de red de ESP-IDF para obtener información de red como la IP.
#include "esp_netif.h"

// Bibliotecas estándar de C para manejo de cadenas y entrada/salida.
#include <string.h>
#include <stdio.h>

// Bibliotecas de lwIP para manejo de sockets y direcciones IP.
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "lwip/dns.h"           // Necesario para obtener la IP del servidor DNS
#include "lwip/ip_addr.h"       // Para manipular direcciones IP (ip4, ip6, etc.)

/**
 * @brief Verifica si hay conexión a internet intentando acceder a una URL que responde con un código 204.
 *
 * @param timeout_ms Tiempo máximo de espera en milisegundos para la solicitud HTTP.
 * @return true si la solicitud fue exitosa y se recibió un código 204, lo que indica conectividad a Internet.
 *         false en caso contrario.
 */
bool check_internet_connection(int timeout_ms) {
    // Configura el cliente HTTP con la URL especial de Google que devuelve 204 si hay acceso a internet.
    esp_http_client_config_t config = {
        .url = "http://clients3.google.com/generate_204",  // URL para testear conectividad
        .method = HTTP_METHOD_GET,                          // Método HTTP GET
        .timeout_ms = timeout_ms,                           // Tiempo máximo de espera
    };

    // Inicializa el cliente HTTP con la configuración anterior.
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;  // Si no se puede inicializar, no hay conexión

    // Realiza la solicitud HTTP.
    esp_err_t err = esp_http_client_perform(client);

    // Obtiene el código de estado HTTP de la respuesta.
    int status = esp_http_client_get_status_code(client);

    // Libera los recursos del cliente HTTP.
    esp_http_client_cleanup(client);

    // Devuelve true si la solicitud fue exitosa y el código fue 204, indicando conexión a internet.
    return (err == ESP_OK && status == 204);
}

/**
 * @brief Verifica si el ESP32 está conectado a un punto de acceso Wi-Fi.
 *
 * @return true si está conectado, false si no.
 */
bool is_wifi_connected(void) {
    wifi_ap_record_t info;  // Estructura que almacenará la información del AP al que está conectado
    return esp_wifi_sta_get_ap_info(&info) == ESP_OK;  // Si se puede obtener info, está conectado
}

/**
 * @brief Obtiene la IP local del ESP32 y la guarda en una cadena de texto.
 *
 * @param ip_str Este es un puntero a un búfer (arreglo de caracteres) donde se escribirá la IP.
 *               El asterisco (*) indica que se está pasando un puntero, es decir, la dirección
 *               de memoria de ese búfer. Esto permite que la función modifique directamente el
 *               contenido del arreglo original desde fuera de la función.
 *
 *               En C, esto se llama "paso por referencia" y se logra mediante el uso de punteros.
 *               En lugar de copiar el valor, se pasa la dirección de memoria para operar sobre
 *               la variable original.
 *
 * @param max_len Tamaño máximo del búfer `ip_str` para evitar desbordamientos.
 *
 * @return ESP_OK si se pudo obtener la IP, ESP_FAIL si hubo algún error.
 *
 * Ejemplo de uso:
 * char ip[16];
 * if (get_local_ip(ip, sizeof(ip)) == ESP_OK) {
 *     printf("Mi IP es: %s\n", ip);  // El búfer 'ip' fue modificado por la función
 * }
 */
 
esp_err_t get_local_ip(char *ip_str, size_t max_len) {
    esp_netif_ip_info_t ip_info;  // Estructura para almacenar la IP local

    // Obtiene el manejador de la interfaz de red Wi-Fi en modo estación (STA).
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return ESP_FAIL;  // Si no se encuentra la interfaz, falla

    // Obtiene la información IP de la interfaz.
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) return ESP_FAIL;  // Si falla la obtención, retorna fallo

	  if (ip_info.ip.addr == 0) {  // IP 0.0.0.0 significa sin IP asignada
        return ESP_FAIL;
    }

    // Convierte la IP a cadena y la escribe en el búfer proporcionado.
    snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}



// Función que obtiene la IP del gateway (puerta de enlace) al que está conectado el ESP32 vía Wi-Fi
esp_err_t get_gateway_ip(char *gw_str, size_t max_len) {
    esp_netif_ip_info_t ip_info;  // Estructura donde se guardará la IP, máscara y gateway

    // Obtiene el handle de la interfaz de red Wi-Fi en modo estación (STA)
    // "WIFI_STA_DEF" es el nombre por defecto de la interfaz Wi-Fi si usas `esp_netif_create_default_wifi_sta()`
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return ESP_FAIL;  // Si no se pudo obtener la interfaz, se retorna error

    // Obtiene la información IP (IP local, máscara y gateway) de la interfaz
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) return ESP_FAIL;

    // Si la dirección del gateway es 0.0.0.0, se considera inválida
    if (ip_info.gw.addr == 0) return ESP_FAIL;

    // Convierte la dirección IP del gateway a cadena legible (ej. "192.168.1.1")
    snprintf(gw_str, max_len, IPSTR, IP2STR(&ip_info.gw));

    // Retorna ESP_OK si todo fue exitoso
    return ESP_OK;
}


// Función que obtiene la dirección IP del servidor DNS principal (índice 0)
esp_err_t get_dns_ip(char *dns_str, size_t max_len) {
    // Obtiene la IP del servidor DNS configurado en la posición 0
    const ip_addr_t* dns_ip = dns_getserver(0);

    // Verifica si el puntero es NULL o si no hay una dirección IP válida (0.0.0.0)
    if (dns_ip == NULL || ip_addr_isany(dns_ip)) return ESP_FAIL;

    // Convierte la IP del servidor DNS a formato legible (ej. "8.8.8.8")
    snprintf(dns_str, max_len, IPSTR, IP2STR(&dns_ip->u_addr.ip4));

    // Retorna ESP_OK si se obtuvo y formateó exitosamente la IP DNS
    return ESP_OK;
}