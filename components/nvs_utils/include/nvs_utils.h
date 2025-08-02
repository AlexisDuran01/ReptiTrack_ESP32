#pragma once
// Esta directiva asegura que este archivo de encabezado solo se incluya una vez por unidad de compilación.
// Evita errores de redefinición durante la compilación del proyecto.

#include "esp_err.h"   // Proporciona el tipo esp_err_t y códigos de error comunes del framework ESP-IDF
#include <stddef.h>    // Define el tipo size_t para manejar tamaños de memoria o arrays


/*
 * Archivo de cabecera (header) para el módulo nvs_utils.
 * 
 * - Contiene las declaraciones públicas de las funciones, tipos y constantes
 *   que provee el módulo para ser usadas en otras partes del programa.
 * 
 * - Sirve como contrato o interfaz: aquí solo se declaran las funciones,
 *   sin implementar la lógica.
 * 
 * - Se incluye en los archivos que quieran usar las funciones de nvs_utils,
 *   permitiendo al compilador conocer sus firmas para hacer llamadas correctas.
 * 
 * - Normalmente, este archivo se encuentra en 'include/' para que sea accesible
 *   desde otras partes del proyecto o incluso desde otros proyectos que quieran usar esta librería
 
 	El .h es la "interfaz pública" (qué puedes usar)
 	Se incluye (#include "nvs_utils.h") en otros archivos para poder llamar a las funciones 
 	implementadas en el .c.
 
 */


/*
 * ¿Qué son `namespace` y `key` en NVS y por qué son necesarios?
 *
 * En ESP-IDF, la NVS (Non-Volatile Storage) permite guardar datos persistentes en memoria flash.
 * Para organizar estos datos de forma estructurada y evitar conflictos, se usan dos conceptos clave:
 *
 * 1. `namespace` (espacio de nombres):
 *    - Es un identificador lógico (como una carpeta virtual) dentro de la NVS.
 *    - Permite agrupar claves relacionadas entre sí.
 *    - Por ejemplo: puedes tener un `namespace` llamado "wifi" para guardar SSID y contraseña,
 *      y otro llamado "mqtt" para guardar parámetros del cliente MQTT.
 *    - Evita colisiones entre claves de distintas partes del programa.
 *    - Debe ser abierto explícitamente con `nvs_open()` antes de usarlo.
 *
 * 2. `key` (clave):
 *    - Es el nombre único que identifica un valor dentro del `namespace`.
 *    - Cada dato que se guarda en NVS se asocia a una clave.
 *    - Por ejemplo: bajo el namespace "mqtt", puedes tener claves como "broker", "port", "user", "pass".
 *    - La longitud máxima de una clave es de 15 caracteres (restringido por la implementación de NVS).
 *
 * Ejemplo de uso práctico - CONFIGURACIÓN MQTT:
 * -----------------------------------------------------------
 * namespace: "mqtt"
 *
 * Claves y valores guardados:
 *   key:   "broker"   -> valor: "mqtt://broker.emqx.io"
 *   key:   "port"     -> valor: 1883 (como entero)
 *   key:   "clientId" -> valor: "esp32_terrario01"
 *   key:   "user"     -> valor: "usuario_mqtt"
 *   key:   "pass"     -> valor: "clave_mqtt"
 *
 * Esto permite que, al iniciar el dispositivo, cargue automáticamente su configuración MQTT desde la NVS,
 * sin necesidad de reconfigurar o recompilar el firmware.
 *
 * Cómo se vería la estructura dentro de la NVS (lógica visual):
 *
 * ┌───────────────────────────┐
 * │      Namespace: mqtt      │
 * ├───────────────┬───────────┤
 * │    broker     │ "mqtt://broker.emqx.io"
 * │    port       │ 1883
 * │    clientId   │ "esp32_terrario01"
 * │    user       │ "usuario_mqtt"
 * │    pass       │ "clave_mqtt"
 * └───────────────┴───────────┘
 *
 * Luego, puedes acceder a cada valor de la siguiente forma:
 *
 * nvs_utils_save_blob("mqtt", "broker", broker_url, strlen(broker_url)+1);
 * nvs_utils_load_blob("mqtt", "clientId", buffer, sizeof(buffer), &len);
 * nvs_utils_erase_key("mqtt", "pass"); // Borrar solo la clave del password
 *
 * Beneficios:
 * - Permite modularidad en los datos guardados.
 * - Mejora la legibilidad y mantenimiento del código.
 * - Facilita la eliminación o recuperación específica de configuraciones.
 * - Ideal para dispositivos configurables en campo sin recompilación.
 */



/**
 * @brief Guarda un bloque binario de datos (blob) en la NVS (memoria flash persistente).
 *
 * Esta función permite almacenar cualquier tipo de dato estructurado (structs, arrays, configuraciones)
 * bajo una clave en un espacio de nombres específico en la NVS.
 *
 * @param[in] namespace   Nombre del espacio de nombres (máximo 15 caracteres). Debe existir en la partición NVS.
 * @param[in] key         Clave única dentro del namespace para identificar los datos (máximo 15 caracteres).
 * @param[in] data        Puntero al bloque de datos que se desea guardar.
 * @param[in] len         Longitud en bytes del bloque de datos.
 *
 * @return esp_err_t
 *         - ESP_OK si el dato fue almacenado correctamente.
 *         - ESP_ERR_NVS_INVALID_HANDLE si hubo un error al abrir el espacio de nombres.
 *         - ESP_ERR_NVS_READ_ONLY si se abrió en modo de solo lectura.
 *         - ESP_ERR_NVS_NOT_ENOUGH_SPACE si no hay espacio suficiente en la partición NVS.
 */

// Se pueden guardar hasta una cadena de 3999 caracteres
esp_err_t nvs_utils_save_blob(const char *namespace, const char *key, const void *data, size_t len);


/**
 * @brief Recupera un bloque de datos binarios desde la NVS.
 *
 * Esta función lee el contenido asociado a una clave, siempre que exista, y lo copia a un buffer proporcionado.
 * Es útil para restaurar configuraciones, estados previos, identificadores, etc.
 *
 * @param[in]  namespace    Espacio de nombres donde se encuentra la clave.
 * @param[in]  key          Clave que identifica el dato.
 * @param[out] data         Puntero al buffer donde se copiarán los datos leídos.
 * @param[in]  max_len      Tamaño máximo del buffer `data`, en bytes.
 * @param[out] actual_len   Puntero donde se almacenará la cantidad de datos efectivamente leída.
 *
 * @return esp_err_t
 *         - ESP_OK si los datos se leyeron correctamente.
 *         - ESP_ERR_NVS_NOT_FOUND si no existe la clave.
 *         - ESP_ERR_NVS_INVALID_LENGTH si el buffer `data` es demasiado pequeño.
 */
esp_err_t nvs_utils_load_blob(const char *namespace, const char *key, void *data, size_t max_len, size_t *actual_len);

/**
 * @brief Elimina una clave y sus datos asociados del almacenamiento NVS.
 *
 * Es útil para limpiar configuraciones, reiniciar valores por defecto o liberar espacio.
 *
 * @param[in] namespace   Espacio de nombres donde se encuentra la clave.
 * @param[in] key         Clave que se desea borrar.
 *
 * @return esp_err_t
 *         - ESP_OK si la clave fue eliminada correctamente.
 *         - ESP_ERR_NVS_NOT_FOUND si la clave no existe.
 */
esp_err_t nvs_utils_erase_key(const char *namespace, const char *key);


/**
 * @brief Inicializa la partición NVS, borrando y reiniciando si es necesario.
 * 
 * Esta función debe llamarse antes de cualquier operación con NVS.
 * Maneja errores típicos de inicialización y garantiza que NVS esté lista.
 */
void nvs_utils_init(void);





