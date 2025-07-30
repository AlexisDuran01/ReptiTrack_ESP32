#pragma once

// Incluye el tipo bool (true/false)
#include <stdbool.h>

// Incluye tipos de error estándar ESP-IDF
#include "esp_err.h"

// Cliente MQTT de ESP-IDF
#include "mqtt_client.h"

// Namespace y clave para guardar las credenciales MQTT en NVS
#define NVS_NAMESPACE "mqtt"
#define NVS_KEY_CRED "credentials"


/**
 * @brief Estructura que representa las credenciales para conectar al broker MQTT.
 * 
 * broker: URI del broker MQTT, por ejemplo "mqtt://broker.miempresa.com"
 * username: nombre de usuario para autenticación (opcional)
 * password: contraseña para autenticación (opcional)
 */
typedef struct {
    char broker[128];
    char username[64];
    char password[64];
} mqtt_credentials_t;


/**
 * @brief Obtiene la instancia singleton del cliente MQTT.
 * 
 * @return Instancia del cliente MQTT (esp_mqtt_client_handle_t) o NULL si no está inicializado.
 */
esp_mqtt_client_handle_t mqtt_cliente_get_instance(void);


/**
 * @brief Inicializa el cliente MQTT con las credenciales guardadas en NVS,
 *        crea la conexión y suscribe a los topics básicos (si es necesario).
 * 
 * Carga las credenciales MQTT previamente guardadas desde NVS.
 * Inicia el cliente MQTT y registra el manejador de eventos.
 */
void mqtt_cliente_init(void);


/**
 * @brief Finaliza y libera el cliente MQTT.
 * 
 * Detiene la conexión MQTT y destruye la instancia.
 * Después de esto mqtt_cliente_get_instance() retornará NULL.
 */
void mqtt_cliente_deinit(void);


/// @brief Publica un mensaje en un topic MQTT específico
/// @param topic Ruta completa del topic (ej: "dispositivo1/sensors/temp")
/// @param payload Contenido a publicar
/// @param qos Calidad del servicio (0, 1 o 2). Si se pasa un valor inválido, se usará 0.
/// @param retain Indica si el mensaje debe ser retenido por el broker (0 o 1). Otros valores se consideran 0.
/// @return true si se pudo publicar correctamente
bool mqtt_cliente_publish(const char *topic, const char *payload, int qos, int retain);


/**
 * @brief Se suscribe a un topic MQTT dado dinámicamente con QoS configurable.
 * 
 * @param topic_filter Topic o filtro para suscribirse (ej. "dispositivo/comandos/#")
 * @param qos Calidad de servicio deseada (0, 1 o 2) para recibir los mensajes.
 *            No se puede aumentar la QoS más allá de la usada por el publicador, solo igualarla o reducirla
 *       		
 * @return true si la suscripción fue exitosa, false si hubo error o cliente no inicializado.
 */
bool mqtt_cliente_subscribe(const char *topic_filter, int qos);



/**
 * @brief Construye la ruta base para topics MQTT a partir de datos almacenados en NVS.
 * 
 * La ruta base :
 *     "reptritrack/{userId}/{espId}"
 * 
 * Los valores userId y espId se leen desde NVS en los namespaces "database" y "dev_info".
 * 
 * @return true si la ruta base fue construida exitosamente, false en caso contrario.
 */
bool mqtt_cliente_build_base_topic(void);


/// @brief Publica un mensaje en un subtopic bajo la ruta base
/// @param subtopic Subruta relativa (ej: "sensors/temperature")
/// @param payload Contenido a publicar
/// @param qos Calidad del servicio (0, 1 o 2). Si se pasa un valor inválido, se usará 0.
/// @param retain Indica si el mensaje debe ser retenido por el broker (0 o 1). Cualquier otro valor será interpretado como 0.
/// @return true si se pudo publicar correctamente

bool mqtt_cliente_publish_with_base(const char *subtopic, const char *payload, int qos, int retain);

/**
 * @brief Se suscribe a un topic usando la ruta base y un subtopic concatenado, con QoS configurable.
 * 
 * Construye el topic completo como "{ruta_base}/{subtopic}" y se suscribe a ese topic.
 * 
 * @param subtopic Subtopic relativo (ej. "actuadores/leds/#")
 * @param qos Calidad de servicio deseada (0, 1 o 2). El QoS recibido será igual o menor al que usa el publicador.
 * @return true si la suscripción fue exitosa, false si falla o cliente no inicializado o ruta base no construida.
 */
bool mqtt_cliente_subscribe_with_base(const char *subtopic, int qos);


/**
 * @brief Guarda las credenciales MQTT en NVS para usarlas después.
 * 
 * @param creds Puntero a la estructura mqtt_credentials_t con credenciales a guardar.
 * @return ESP_OK si se guardó correctamente, o un código de error en caso contrario.
 */
esp_err_t mqtt_credentials_save(const mqtt_credentials_t *creds);


/**
 * @brief Carga las credenciales MQTT almacenadas en NVS.
 * 
 * @param creds Puntero a la estructura mqtt_credentials_t donde se almacenarán las credenciales leídas.
 * @return ESP_OK si se cargaron correctamente, o un código de error en caso contrario.
 */
esp_err_t mqtt_credentials_load(mqtt_credentials_t *creds);


/**
 * @brief Tipo para función callback que recibe mensajes MQTT.
 * 
 * Se llama cuando llega un mensaje MQTT en algún topic suscrito.
 * 
 * @param topic       Topic en el que llegó el mensaje.
 * @param payload     Contenido del mensaje.
 * @param payload_len Longitud del payload.
 */
typedef void (*mqtt_message_callback_t)(const char *topic, const char *payload, int payload_len);


/**
 * @brief Registra un callback para recibir mensajes MQTT.
 * 
 * Soporta hasta MAX_MQTT_LISTENERS (5) callbacks registrados.
 * 
 * @param callback Puntero a función a registrar.
 * @return true si el callback se registró correctamente, false si se llegó al límite de listeners.
 */
bool mqtt_cliente_register_listener(mqtt_message_callback_t callback);


/**
 * @brief Elimina un callback registrado para mensajes MQTT.
 * 
 * @param callback Puntero a función a eliminar.
 * @return true si se encontró y eliminó el callback, false si no se encontró.
 */
bool mqtt_cliente_unregister_listener(mqtt_message_callback_t callback);
