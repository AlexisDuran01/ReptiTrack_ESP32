// Incluye el encabezado de funciones y estructuras propias del módulo MQTT.
// Este archivo contiene las declaraciones públicas del cliente MQTT (como init, publish, etc.)
#include "mqtt_cliente.h"

// Incluye el sistema de logging de ESP-IDF para imprimir mensajes en consola.
// Esto es útil para depurar (debug) y ver el comportamiento del cliente MQTT en tiempo real.
#include "esp_log.h"

// Incluye el cliente MQTT oficial de ESP-IDF.
// Contiene la API para inicializar, conectar, publicar, suscribirse y recibir mensajes MQTT.
#include "mqtt_client.h"

// Incluye funciones auxiliares personalizadas para acceder a NVS (almacenamiento no volátil).
// Este archivo `nvs_utils.h` es un wrapper sobre el API oficial de NVS, para facilitar guardar y cargar blobs como las credenciales MQTT, userId, espId, etc.
#include "nvs_utils.h"

// Incluye funciones estándar de C para manipular cadenas de texto (como strlen, snprintf, strcmp, etc.).
#include <string.h>

#include "esp_crt_bundle.h"  // Asegúrate de incluir esto

#include "rtc_time.h"            

#include "sensors_manager.h"

/// Etiqueta de logging para este módulo MQTT.
// Se utiliza como prefijo en todos los mensajes de log generados por ESP_LOGI, ESP_LOGE, etc.
// Facilita identificar de qué módulo vienen los logs cuando depuras.
static const char *TAG = "mqtt_cliente";

/// Instancia Singleton del cliente MQTT.
// Esta variable contiene el cliente MQTT principal que se usará en toda la aplicación.
// Solo se inicializa una vez y se reutiliza en todas las funciones para publicar, suscribirse, etc.
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

/// Número máximo de listeners que se pueden registrar para recibir mensajes entrantes MQTT.
// Define cuántos callbacks puedes registrar con mqtt_cliente_register_listener().
#define MAX_MQTT_LISTENERS 5

/// Lista de callbacks registrados para recibir eventos MQTT entrantes.
// Cada vez que llega un mensaje MQTT, se llama a todos los callbacks almacenados aquí.
static mqtt_message_callback_t s_listeners[MAX_MQTT_LISTENERS] = {0};

/// Contador de cuántos listeners están actualmente registrados.
static int s_listener_count = 0;

/// Topic base generado dinámicamente desde NVS.
// Este topic base se usa para construir automáticamente rutas completas de publicación y suscripción.
// Por ejemplo: reptritrack/user123/esp01
static char s_base_topic[64] = {0};

/// @brief Devuelve la instancia Singleton del cliente MQTT.
// Esta función permite a otras partes del código acceder directamente al cliente MQTT
// para casos donde se necesite usar la API de esp_mqtt_client directamente.
esp_mqtt_client_handle_t mqtt_cliente_get_instance(void) {
    return s_mqtt_client;
}


//
// -----------------------------
// GESTIÓN DE LISTENERS
// -----------------------------

/// @brief Registra un listener (callback) para recibir mensajes MQTT entrantes.
/// 
/// Este callback se ejecutará automáticamente cada vez que llegue un mensaje MQTT
/// (dentro del evento `MQTT_EVENT_DATA`). Permite desacoplar la lógica de recepción
/// para que otros módulos puedan reaccionar sin depender del cliente MQTT.
///
/// @param callback Puntero a una función que será llamada con los datos del mensaje recibido.
/// @return true si se registró exitosamente, false si se alcanzó el límite máximo.
bool mqtt_cliente_register_listener(mqtt_message_callback_t callback) {
    
    // Verifica si ya se alcanzó el número máximo de listeners permitidos
    if (s_listener_count >= MAX_MQTT_LISTENERS) {
        ESP_LOGW(TAG, "No se pueden registrar más listeners MQTT");
        return false;  // Rechaza el registro si ya hay demasiados
    }

    // Agrega el callback al arreglo de listeners
    s_listeners[s_listener_count++] = callback;

    // Indica que el registro fue exitoso
    return true;
}



/// @brief Elimina un listener (callback) previamente registrado para mensajes MQTT.
///
/// Esto permite que un módulo deje de recibir mensajes si ya no los necesita,
/// por ejemplo, cuando se destruye una vista o se desactiva una funcionalidad.
///
/// @param callback El mismo puntero a función que fue registrado previamente.
/// @return true si se eliminó exitosamente, false si no se encontró.
bool mqtt_cliente_unregister_listener(mqtt_message_callback_t callback) {
    bool found = false;

    // Recorre todos los listeners registrados
    for (int i = 0; i < s_listener_count; i++) {
        // Si encuentra el callback a eliminar
        if (s_listeners[i] == callback) {
            found = true;

            // Recorre el arreglo hacia atrás para compactarlo,
            // moviendo los siguientes listeners una posición antes
            for (int j = i; j < s_listener_count - 1; j++) {
                s_listeners[j] = s_listeners[j + 1];
            }

            // Limpia la última posición y disminuye el contador de listeners
            s_listeners[--s_listener_count] = NULL;

            // Ya que se eliminó, se puede salir del ciclo
            break;
        }
    }

    // Si no se encontró el callback, lanza una advertencia
    if (!found) {
        ESP_LOGW(TAG, "Listener a eliminar no encontrado");
    }

    return found;
}



void publish_estado_conectado(void) {
    char time_str[64];  // Suficiente para "YYYY-MM-DD HH:MM:SS"

    // Obtener la fecha/hora actual del RTC en formato legible
    esp_err_t err = rtc_time_get_formatted_readable(time_str, sizeof(time_str));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RTC no sincronizado, no se puede publicar estado conectado con fecha");
        return;
    }

    // Crear el payload JSON
    char json_payload[128];
    int ret = snprintf(json_payload, sizeof(json_payload),
                       "{\"fecha\":\"%s\",\"estatus\":\"conectado\"}",
                       time_str);

    if (ret < 0 || ret >= (int)sizeof(json_payload)) {
        ESP_LOGE(TAG, "Error creando JSON para estado conectado");
        return;
    }

    // Publicar al tópico "estatus"
    bool publicado = mqtt_cliente_publish_with_base("estatus", json_payload, 1, false);
    if (!publicado) {
        ESP_LOGW(TAG, "No se pudo publicar estado conectado");
    } else {
        ESP_LOGI(TAG, "Estado conectado publicado correctamente");
    }
}


//
// -----------------------------
// HANDLER DE EVENTOS MQTT
// -----------------------------

/// @brief Función interna que maneja todos los eventos del cliente MQTT.
///
/// Esta función es llamada automáticamente por el sistema de eventos de ESP-IDF
/// cuando ocurren eventos relacionados con la conexión MQTT: conexión, desconexión,
/// publicación, recepción de mensajes, errores, etc.
///
/// @param handler_args Argumentos del handler (no usados aquí)
/// @param base Tipo de evento (normalmente MQTT_EVENTS)
/// @param event_id Identificador del evento (e.g. MQTT_EVENT_CONNECTED)
/// @param event_data Información específica del evento
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    // Convierte el puntero genérico a estructura de evento MQTT
    esp_mqtt_event_handle_t event = event_data;

    // Manejador general para múltiples tipos de evento
    switch (event_id) {
        // Evento lanzado al conectar exitosamente al broker
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT conectado");
            // Aquí podrías hacer una suscripción por defecto si deseas
            
			publish_estado_conectado();
		
		    if (sensors_manager_is_initialized()) {
                ESP_LOGI(TAG, "Publicando datos iniciales de sensores tras conexion MQTT \n \n \n");
                sensors_manager_start_publish();
            } else {
                ESP_LOGW(TAG, "Manager de sensores no inicializado, no se publican datos");
            }

			    
            break;

        // Evento lanzado al desconectarse del broker
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT desconectado");
            break;

        // Evento cuando una suscripción fue aceptada por el broker
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "Suscripción exitosa, msg_id=%d", event->msg_id);
            break;

        // Evento cuando una desuscripción fue confirmada por el broker
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "Desuscripción, msg_id=%d", event->msg_id);
            break;

        // Evento cuando un mensaje fue publicado con éxito
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "Mensaje publicado, msg_id=%d", event->msg_id);
            break;

        // Evento cuando se recibe un mensaje desde el broker (al estar suscrito)
        case MQTT_EVENT_DATA:
            // Muestra en el log el topic donde llegó el mensaje
            ESP_LOGI(TAG, "Mensaje recibido en topic: %.*s", event->topic_len, event->topic);
            // Muestra el contenido del mensaje recibido
            ESP_LOGI(TAG, "Contenido: %.*s", event->data_len, event->data);

            // Recorre todos los listeners registrados y les notifica del mensaje
            for (int i = 0; i < s_listener_count; i++) {
                if (s_listeners[i]) {
                    // Llama al callback registrado pasando topic, contenido y tamaño
                    s_listeners[i](event->topic, event->data, event->data_len);
                }
            }
            break;

        // Evento de error general en la comunicación MQTT
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Error MQTT");
            break;

        // Otros eventos no manejados explícitamente
        default:
            break;
    }
}

//
// -----------------------------
// INICIALIZACIÓN Y FINALIZACIÓN
// -----------------------------

/// @brief Inicializa el cliente MQTT y lo conecta al broker.
///
/// Esta función carga las credenciales desde NVS, configura el cliente MQTT,
/// registra el manejador de eventos y lo inicia para comenzar la comunicación.
void mqtt_cliente_init(void) {
    // Estructura temporal para cargar credenciales desde NVS
    mqtt_credentials_t creds = {0};
    
    ESP_LOGI(TAG, "Proceso de Cliente MQTT iniciado");


    // Intenta cargar las credenciales almacenadas
    esp_err_t err = mqtt_credentials_load(&creds);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error al cargar credenciales MQTT desde NVS: %s", esp_err_to_name(err));
        return; // No se puede continuar sin credenciales
    }
    
       // ---> Intentar construir la ruta base
    if (!mqtt_cliente_build_base_topic()) {
        ESP_LOGW(TAG, "No se pudo construir ruta base MQTT");
    }else {
        ESP_LOGI(TAG, "Se construyo la ruta base MQTT");
	}

    // Log informativo con las credenciales (sin mostrar password por seguridad)
    ESP_LOGI(TAG, "Credenciales cargadas: broker=%s, usuario=%s", creds.broker, creds.username);


    // Estructura de configuración del cliente MQTT (según las credenciales cargadas)
		esp_mqtt_client_config_t mqtt_cfg = {
		    .broker.address.uri = creds.broker,
   			 .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,  // usa el bundle de certificados
		    .credentials.username = strlen(creds.username) > 0 ? creds.username : NULL,
		    .credentials.authentication.password = strlen(creds.password) > 0 ? creds.password : NULL,
		    .network.reconnect_timeout_ms = 10000,
		    .network.disable_auto_reconnect = false,
		};
    // Crea e inicializa el cliente MQTT
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt_client) {
        ESP_LOGE(TAG, "Fallo al crear el cliente MQTT");
        return;
    }

    // Registra el manejador de eventos (se ejecutará ante cualquier evento MQTT)
    //Este callback (función) que tú le pasamos  al cliente MQTT de ESP-IDF, para que ella la ejecute automáticamente cuando ocurra cierto evento.
    // Cuando ocurra cualquier evento MQTT, llama a esta función (mqtt_event_handler)”   
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    // Inicia el cliente MQTT (intenta conectarse automáticamente al broker)
    err = esp_mqtt_client_start(s_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error al iniciar cliente MQTT: %s", esp_err_to_name(err));
        return;
    }

		ESP_LOGW(TAG, "Cliente MQTT iniciado");
}


/// @brief Detiene y destruye el cliente MQTT.
///
/// Esta función debe llamarse si ya no se va a utilizar el cliente MQTT,
/// por ejemplo, cuando se desconecta el dispositivo o cambia de usuario.
void mqtt_cliente_deinit(void) {
    if (s_mqtt_client) {
        // Detiene la conexión con el broker
        esp_mqtt_client_stop(s_mqtt_client);

        // Libera los recursos asociados al cliente
        esp_mqtt_client_destroy(s_mqtt_client);

        // Limpia la referencia global
        s_mqtt_client = NULL;
    }
}


//
// -----------------------------
// CREDENCIALES (NVS)
// -----------------------------

/// @brief Guarda las credenciales MQTT en NVS
/// @param creds Puntero a la estructura que contiene broker, usuario y contraseña
/// @return ESP_OK si se guardó exitosamente, o un código de error si falló
esp_err_t mqtt_credentials_save(const mqtt_credentials_t *creds) {
    // Validar que el puntero no sea NULL
    if (!creds) return ESP_ERR_INVALID_ARG;

    // Guardar las credenciales como un blob binario en el namespace y clave definidos
    return nvs_utils_save_blob(NVS_NAMESPACE, NVS_KEY_CRED, creds, sizeof(mqtt_credentials_t));
}

/// @brief Carga las credenciales MQTT previamente guardadas en NVS
/// @param creds Puntero a una estructura donde se cargarán los datos
/// @return ESP_OK si se cargó correctamente, o un código de error si falló
esp_err_t mqtt_credentials_load(mqtt_credentials_t *creds) {
    // Validar que el puntero no sea NULL
    if (!creds) return ESP_ERR_INVALID_ARG;

    // Variable que almacenará el tamaño real leído desde NVS
    size_t actual_len = 0;

    // Leer las credenciales desde NVS usando la clave y namespace definidos
    //Se guarda en un tipo objeto mqtt que se definio entonces no es necesario deserializar la informacion
    esp_err_t err = nvs_utils_load_blob(NVS_NAMESPACE, NVS_KEY_CRED, creds, sizeof(mqtt_credentials_t), &actual_len);

    // Validar que el tamaño leído coincida con el esperado
    if (err == ESP_OK && actual_len != sizeof(mqtt_credentials_t)) {
        ESP_LOGW(TAG, "Tamaño inesperado de credenciales (%zu bytes)", actual_len);
        return ESP_ERR_INVALID_SIZE;
    }

    // Retornar el resultado de la operación (ESP_OK o código de error)
    return err;
}

//
// -----------------------------
// TOPIC BASE (ruta base MQTT)
// -----------------------------

/// @brief Construye la ruta base del topic MQTT usando userId y espId almacenados en NVS
/// @details Lee el identificador de usuario ("userId") desde el namespace "database"
///          y el identificador del dispositivo ("espId") desde el namespace "dev_info".
///          Luego, forma un topic base con el siguiente formato:
///          "reptritrack/<userId>/<espId>", que se usará como prefijo para publicaciones y suscripciones.
/// @return true si la ruta fue construida correctamente, false si hubo error de lectura o formato.
bool mqtt_cliente_build_base_topic(void) {
    char user_id[64] = {0};  // Buffer para almacenar el userId leído desde NVS
    char esp_id[16] = {0};   // Buffer para almacenar el espId leído desde NVS
    size_t len = 0;          // Longitud real de los datos leídos

    // --- Leer el userId desde NVS (namespace "database", clave "user_id") ---
    len = sizeof(user_id);
    if (nvs_utils_load_blob("database", "user_id", user_id, len, &len) != ESP_OK || len == 0) {
        ESP_LOGW(TAG, "No se pudo leer userId de NVS");
        return false;
    }

    // --- Leer el espId desde NVS (namespace "dev_info", clave "esp_id") ---
    len = sizeof(esp_id);
    if (nvs_utils_load_blob("dev_info", "esp_id", esp_id, len, &len) != ESP_OK || len == 0) {
        ESP_LOGW(TAG, "No se pudo leer espId de NVS");
        return false;
    }

    // --- Construir la ruta base del topic MQTT: reptritrack/<userId>/<espId> ---
    int ret = snprintf(s_base_topic, sizeof(s_base_topic), "reptritrack/%s/%s", user_id, esp_id);

    // Validar si ocurrió un error al construir el string o si se excedió el tamaño del buffer
    if (ret < 0 || ret >= (int)sizeof(s_base_topic)) {
        ESP_LOGE(TAG, "Error construyendo ruta base MQTT");
        return false;
    }

    // Log exitoso de la ruta construida
    ESP_LOGI(TAG, "Ruta base MQTT construida: %s", s_base_topic);
    return true;
}


//
// -----------------------------
// PUBLICACIÓN Y SUSCRIPCIÓN
// -----------------------------


/*
 * QoS (Quality of Service) en MQTT define la garantía de entrega del mensaje:
 
 *  0 - Entrega como máximo una vez (sin confirmación, rápido, sin garantía)
 *  1 - Entrega al menos una vez (confirmado, puede duplicarse)
 *  2 - Entrega exactamente una vez (confirmado, sin duplicados, más lento)
 *
 * Recomendaciones:
 *  - QoS 0: sensores frecuentes (temperatura, batería)
 *  - QoS 1: estados, comandos importantes
 *  - QoS 2: solo si es crítico evitar duplicados (raro en IoT)
 */



/// @brief Publica un mensaje en un topic MQTT específico
/// @param topic Ruta completa del topic (ej: "dispositivo1/sensors/temp")
/// @param payload Contenido a publicar
/// @param qos Calidad del servicio (0, 1 o 2). Si se pasa un valor inválido, se usará 0.
/// @param retain Indica si el mensaje debe ser retenido por el broker (0 o 1). Otros valores se consideran 0.
/// @return true si se pudo publicar correctamente
bool mqtt_cliente_publish(const char *topic, const char *payload, int qos, int retain) {
    // Asegurarse de que el cliente MQTT está inicializado
    if (!s_mqtt_client) {
        ESP_LOGW(TAG, "Cliente MQTT no inicializado");
        return false;
    }

    // Validar QoS: solo se permiten valores 0, 1 o 2. Usar 0 por defecto si no es válido.
    if (qos < 0 || qos > 2) {
        ESP_LOGW(TAG, "QoS inválido (%d), usando 0 por defecto", qos);
        qos = 0;
    }

    // Validar retain: debe ser 0 o 1. Otros valores se consideran 0.
    if (retain != 0 && retain != 1) {
        ESP_LOGW(TAG, "Retain inválido (%d), usando 0 por defecto", retain);
        retain = 0;
    }

    // Publicar directamente en el topic especificado
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, qos, retain);

    return (msg_id != -1);
}

/**
 * @brief Se suscribe a un topic MQTT completo con QoS configurable.
 * 
 * @param topic_filter Ruta completa del topic a suscribirse (ej: "dispositivo/sensor/temp")
 * @param qos Calidad de servicio (0, 1 o 2). El QoS recibido será igual o menor al que usa el publicador.
 * @return true si la suscripción fue exitosa, false si falló o el cliente no está inicializado.
 */
bool mqtt_cliente_subscribe(const char *topic_filter, int qos) {
    // Verificar que el cliente MQTT esté activo
    if (!s_mqtt_client) {
        ESP_LOGW(TAG, "Cliente MQTT no inicializado");
        return false;
    }

    // Validar el valor de QoS (permitidos: 0, 1 o 2)
    if (qos < 0 || qos > 2) {
        ESP_LOGW(TAG, "QoS inválido (%d), usando 0 por defecto", qos);
        qos = 0;
    }

    // Solicitar la suscripción al topic con el QoS especificado
    int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, topic_filter, qos);

    // Si el msg_id es -1, la suscripción falló
    return (msg_id != -1);
}


/// @brief Publica un mensaje en un subtopic bajo la ruta base
/// @param subtopic Subruta relativa (ej: "sensors/temperature")
/// @param payload Contenido a publicar
/// @param qos Calidad del servicio (0, 1 o 2). Si se pasa un valor inválido, se usará 0.
/// @param retain Indica si el mensaje debe ser retenido por el broker (0 o 1). Cualquier otro valor será interpretado como 0.
/// @return true si se pudo publicar correctamente
bool mqtt_cliente_publish_with_base(const char *subtopic, const char *payload, int qos, int retain) {
    // Asegurarse de que el cliente MQTT está inicializado
    if (!s_mqtt_client) {
        ESP_LOGW(TAG, "Cliente MQTT no inicializado");
        return false;
    }

    // Verificar que la ruta base del topic ya fue construida previamente
    if (!s_base_topic[0]) {
        ESP_LOGW(TAG, "Ruta base no construida");
        return false;
    }

    // Validar QoS: solo se permiten valores 0, 1 o 2. Usar 0 por defecto si no es válido.
    if (qos < 0 || qos > 2) {
        ESP_LOGW(TAG, "QoS inválido (%d), usando 0 por defecto", qos);
        qos = 0;
    }

    // Validar retain: debe ser 0 o 1. Otros valores se consideran 0.
    if (retain != 0 && retain != 1) {
        ESP_LOGW(TAG, "Retain inválido (%d), usando 0 por defecto", retain);
        retain = 0;
    }

    // Construye el topic completo concatenando la ruta base con el subtopic
    char topic[128];
    int ret = snprintf(topic, sizeof(topic), "%s/%s", s_base_topic, subtopic);
    if (ret < 0 || ret >= (int)sizeof(topic)) {
        ESP_LOGE(TAG, "Error construyendo topic completo");
        return false;
    }

    // Publicar en el topic construido con los parámetros especificados
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, qos, retain);

    return (msg_id != -1);
}


/**
 * @brief Se suscribe a un topic usando la ruta base y un subtopic concatenado, con QoS configurable.
 * 
 * Construye el topic completo como "{ruta_base}/{subtopic}" y se suscribe a ese topic.
 * 
 * @param subtopic Subtopic relativo (ej. "actuadores/leds/#")
 * @param qos Calidad de servicio deseada (0, 1 o 2). El QoS recibido será igual o menor al que usa el publicador.
 * @return true si la suscripción fue exitosa, false si falla o cliente no inicializado o ruta base no construida.
 */
bool mqtt_cliente_subscribe_with_base(const char *subtopic, int qos) {
   // Verificar que el cliente MQTT esté activo
    if (!s_mqtt_client) {
        ESP_LOGW(TAG, "Cliente MQTT no inicializado");
        return false;
    }

    // Verificar que la ruta base ya fue definida
    if (!s_base_topic[0]) {
        ESP_LOGW(TAG, "Ruta base no construida");
        return false;
    }

    // Validar QoS (permitidos: 0, 1, 2)
    if (qos < 0 || qos > 2) {
        ESP_LOGW(TAG, "QoS inválido (%d), usando 0 por defecto", qos);
        qos = 0;
    }

    // Construir el topic completo
    char topic[128];
    int ret = snprintf(topic, sizeof(topic), "%s/%s", s_base_topic, subtopic);
    if (ret < 0 || ret >= (int)sizeof(topic)) {
        ESP_LOGE(TAG, "Error construyendo topic completo");
        return false;
    }

    // Solicitar la suscripción al topic completo con QoS especificado
    int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, topic, qos);

    return (msg_id != -1);
}
