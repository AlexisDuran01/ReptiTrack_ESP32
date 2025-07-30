/*
 * Archivo: app_prov.c
 * Descripción:
 * Implementa la lógica principal para el proceso de provisionamiento Wi-Fi en el ESP32,
 * permitiendo que el usuario configure la red Wi-Fi del dispositivo de manera inalámbrica usando BLE o SoftAP.
 * Incluye la inicialización de hardware (LED y botón), la gestión de tareas para indicar el estado del provisioning,
 * el manejo de eventos del proceso de provisionamiento, y la lógica para reprovisionar el dispositivo si el usuario
 * mantiene presionado el botón durante 3 segundos.
 
 */

#include "prov_common.h"                          // Declaraciones comunes para el provisioning Wi-Fi
#include "esp_log.h"                              // Permite mostrar mensajes en la consola para depuración
#include "esp_err.h"                              // Maneja los códigos de error de ESP-IDF
#include <wifi_provisioning/manager.h>            // Permite gestionar el proceso de provisionamiento Wi-Fi
#include <wifi_provisioning/scheme_ble.h>         // Permite usar BLE como método de provisionamiento
#include <wifi_provisioning/scheme_softap.h>      // Permite usar SoftAP como método alternativo
#include <inttypes.h>                             // Permite imprimir números grandes en consola
#include "esp_netif.h"                            // Maneja la interfaz de red del ESP32
#include "esp_event.h"                            // Permite manejar eventos del sistema
#include "esp_wifi.h"                             // Permite controlar el Wi-Fi del ESP32
#include "esp_srp.h"                              // Permite usar seguridad avanzada para el provisionamiento
#include "driver/gpio.h"                          // Permite controlar los pines de entrada/salida (LED y botón)
#include <freertos/FreeRTOS.h>                    // Sistema operativo en tiempo real usado por el ESP32
#include <freertos/task.h>                        // Permite crear y manejar tareas (procesos ligeros)
#include "nvs_flash.h"                            // Permite guardar y borrar datos en la memoria interna
#include "sec2_params.h"                          // Incluye los datos de seguridad para el provisionamiento
#include "pin_config_wifi.h"                      // Define qué pines se usan para el LED y el botón
#include <string.h>                               // Para strlen, strcpy, strcmp, etc.
#include "cJSON.h"  // Librería para parsear (deserializar) y generar (serializar) datos en formato JSON, útil para interpretar comandos o enviar información estructurada (por ejemplo, en mensajes MQTT).
#include "esp_netif.h"
#include "nvs_utils.h"               
#include "conn_manager.h"               
#include "mqtt_cliente.h"

static const char *TAG = "wifi_prov";             // Etiqueta para los mensajes en consola

static TaskHandle_t led_task_handle = NULL;       // Identificador de la tarea del LED
static bool led_blink = false;                    // Indica si el LED debe parpadear o quedarse fijo
static bool provisioned_global = false;           // Indica si el dispositivo ya está configurado

// Indica a la aplicacion movil que debe usar Bluetooth Low Energy (BLE) para comunicarse con el ESP32.
#define PROV_TRANSPORT_BLE      "ble"			  

#define EXAMPLE_PROV_SEC2_USERNAME          "reptitrack"
#define EXAMPLE_PROV_SEC2_PWD               "xp4tzq7" // Contraseña para el provisionamiento

#define WIFI_MAXIMUM_RETRY  5

static int s_retry_num = 0;


// -----------------------------------------------------------------------------
// Inicializa los GPIOs para el LED y el botón de reprovisionamiento
static void init_led_and_button(void) {
    gpio_config_t io_conf_led = {
        .pin_bit_mask = (1ULL << PROV_LED_GPIO), // Selecciona el pin del LED usando una máscara de bits.
                                                 // ¿Para qué sirve? Esta expresión pone en 1 solo el bit correspondiente al número de pin (PROV_LED_GPIO).
                                                 // Así, le indica a gpio_config qué pin configurar como salida, permitiendo seleccionar cualquier GPIO de forma flexible.        
        .mode = GPIO_MODE_OUTPUT,                // Configura el pin como salida
        .pull_up_en = GPIO_PULLUP_DISABLE,       // Desactiva resistencia pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,   // Desactiva resistencia pull-down
        .intr_type = GPIO_INTR_DISABLE           // Sin interrupciones
    };
    gpio_config(&io_conf_led);                   // Aplica la configuración al pin del LED

    gpio_config_t io_conf_btn = {
        .pin_bit_mask = (1ULL << PROV_BUTTON_GPIO), // Máscara de bits para seleccionar el pin del botón
        .mode = GPIO_MODE_INPUT,                 // Configura el pin como entrada
        .pull_up_en = GPIO_PULLUP_DISABLE,       // Desactiva resistencia pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,   // Desactiva resistencia pull-down
        .intr_type = GPIO_INTR_DISABLE           // Sin interrupciones
    };
    gpio_config(&io_conf_btn);                   // Aplica la configuración al pin del botón

    gpio_set_level(PROV_LED_GPIO, 1);            // Enciende el LED por defecto (nivel alto)
}

// -----------------------------------------------------------------------------
// Tarea que controla el LED: parpadea si led_blink es true, fijo si es false
// Una "tarea" en FreeRTOS es como un hilo o proceso ligero que se ejecuta de forma independiente dentro del microcontrolador.
// Permite que el ESP32 realice varias acciones al mismo tiempo, por ejemplo, controlar el LED mientras espera eventos o botones.
// Cada tarea tiene su propia función y puede ejecutarse en paralelo con otras tareas del sistema.
static void led_task(void *pvParameter) {
    while (1) {                                 // Bucle infinito
        if (led_blink) {                        // Si debe parpadear
            gpio_set_level(PROV_LED_GPIO, 1);   // Enciende el LED
            vTaskDelay(pdMS_TO_TICKS(300));     // Espera 300 ms
            gpio_set_level(PROV_LED_GPIO, 0);   // Apaga el LED
            vTaskDelay(pdMS_TO_TICKS(300));     // Espera 300 ms
        } else {
            gpio_set_level(PROV_LED_GPIO, 1);   // Mantiene el LED encendido
            vTaskDelay(pdMS_TO_TICKS(100));     // Espera 100 ms
        }
    }
}

// -----------------------------------------------------------------------------
// Espera a que el botón esté presionado durante 3 segundos continuos
// Si se cumple, devuelve true para activar el reprovisionamiento
static bool wait_button_3s(void) {
    int pressed_time = 0;                       // Tiempo acumulado de presión
    const int check_interval_ms = 50;           // Intervalo de revisión (ms)
    while (gpio_get_level(PROV_BUTTON_GPIO) == 1) { // Mientras el botón esté presionado (nivel alto)
        vTaskDelay(pdMS_TO_TICKS(check_interval_ms)); // Espera un poco
        pressed_time += check_interval_ms;           // Suma el tiempo
        if (pressed_time >= 3000) {                  // Si llegó a 3 segundos
            return true;                             // Devuelve verdadero
        }
    }
    return false; // Si se soltó antes, devuelve falso
}

// -----------------------------------------------------------------------------
// Tarea que corre siempre en segundo plano para detectar si el usuario quiere reprovisionar
// Si detecta el botón presionado 3 segundos, borra la NVS y reinicia el dispositivo
static void reprov_button_task(void *pvParameter) {
    while (1) {                                   // Bucle infinito
        if (wait_button_3s()) {                   // Si el botón se mantuvo presionado 3s
            ESP_LOGI(TAG, "Boton presionado 3s. Borrando NVS y reiniciando para reprovisionar");
            vTaskDelay(pdMS_TO_TICKS(100));       // Espera un poco para evitar rebotes
            ESP_ERROR_CHECK(nvs_flash_erase());   // Borra la configuración guardada en la memoria no volátil
            esp_restart();                        // Reinicia el dispositivo para empezar de nuevo el proceso de conexión
        }
        vTaskDelay(pdMS_TO_TICKS(100));           // Espera un poco antes de volver a revisar
    }
}

/**
 * @brief Inicializa el manager de provisión WiFi.
 * Esta función configura el esquema de provisioning (BLE o SoftAP) y lo inicializa.
 * @return esp_err_t Código de error de ESP-IDF.
 */
esp_err_t my_wifi_prov_mgr_init(void)
{
    ESP_LOGI(TAG, "Inicializando WiFi Provisioning Manager");

    wifi_prov_mgr_config_t config = {
#ifdef CONFIG_EXAMPLE_PROV_TRANSPORT_BLE
        .scheme = wifi_prov_scheme_ble,           // Usa BLE para recibir los datos
#elif defined(CONFIG_EXAMPLE_PROV_TRANSPORT_SOFTAP)
        .scheme = wifi_prov_scheme_softap,        // O usa SoftAP si está configurado así
#else
#error "Debes definir un esquema de provisión en la configuración"
#endif
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE // Sin handler de eventos personalizado
    };

    return wifi_prov_mgr_init(config);            // Inicia el manager de provisionamiento
}

/**
 * @brief Inicia el proceso de provisión WiFi.
 * Llama a la función de ESP-IDF para iniciar el provisioning con los parámetros de seguridad y nombre de servicio.
 * @param security Tipo de seguridad (por ejemplo, SRP6a).
 * @param sec_params Puntero a los parámetros de seguridad (salt, verifier, etc).
 * @param service_name Nombre del servicio BLE/SoftAP.
 * @param service_key Clave opcional para el servicio.
 * @return esp_err_t Código de error de ESP-IDF.
 */
esp_err_t my_wifi_prov_mgr_start(int security, const void *sec_params, const char *service_name, const char *service_key)
{
    ESP_LOGI(TAG, "Iniciando provision WiFi");
    return wifi_prov_mgr_start_provisioning(security, sec_params, service_name, service_key);
}

/**
 * @brief Libera los recursos del manager de provisión.
 * Llama a la función de ESP-IDF para liberar la memoria y recursos usados por el provisioning.
 */
void my_wifi_prov_mgr_deinit(void)
{
    ESP_LOGI(TAG, "Liberando recursos del WiFi Provisioning Manager");
    wifi_prov_mgr_deinit();
}

/**
 * @brief Imprime el código QR para la provisión.
 * Genera y muestra en consola un enlace QR que puede ser escaneado por la app de Espressif para facilitar el proceso de emparejamiento.
 * @param service_name Nombre del servicio BLE/SoftAP.
 * @param username Usuario para el provisioning (solo Security 2).
 * @param pop Prueba de posesión (contraseña).
 * @param transport Tipo de transporte ("ble" o "softap").
 */
void my_wifi_prov_print_qr(const char *service_name, const char *username, const char *pop, const char *transport)
{
    ESP_LOGI(TAG, "Generado codigo QR para provisionamiento...");

    // Validación de parámetros obligatorios - evita crashes si faltan datos críticos
    if (!service_name || !transport) {
        ESP_LOGW(TAG, "No se puede generar el código QR. Faltan datos obligatorios.");
        return; // Sale de la función sin hacer nada si faltan parámetros
    }

    char payload[200] = {0}; // Buffer para almacenar el JSON del código QR (inicializado en ceros)
    
    // Lógica condicional: genera diferentes formatos de JSON según si hay contraseña o no
    if (pop && strlen(pop) > 0) { // Si hay contraseña definida y no está vacía
#if CONFIG_EXAMPLE_PROV_SECURITY_VERSION_2 // Para Security 2 (SRP6a) incluye username
        snprintf(payload, sizeof(payload),
                 "{\"ver\":\"v1\",\"name\":\"%s\",\"username\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
                 service_name, username ? username : "reptitrack", pop, transport);
#else // Para Security 1 no incluye username
        snprintf(payload, sizeof(payload),
                 "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
                 service_name, pop, transport);
#endif
    } else {
        // Para SRP6a sin contraseña - solo incluye nombre, usuario y transporte
        // El campo "pop" se omite completamente porque SRP6a usa verificadores criptográficos
        snprintf(payload, sizeof(payload),
                 "{\"ver\":\"v1\",\"name\":\"%s\",\"username\":\"%s\",\"transport\":\"%s\"}",
                 service_name, username ? username : "reptitrack", transport);
    }

    // Logs informativos para depuración
    ESP_LOGI(TAG, "Datos del QR generados: %s", payload);
    // Imprime el enlace 
    ESP_LOGI(TAG, "Si el codigo QR no es visible, copia y pega esta URL en un navegador:");
   	ESP_LOGI(TAG, "https://espressif.github.io/esp-jumpstart/qrcode.html?data=%s\n\n", payload);
}


// Función manejadora de eventos del sistema relacionados con Wi-Fi 
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
		// Este código se ejecuta cuando el sistema Wi-Fi indica que la interfaz Wi-Fi
		// en modo estación (Station-STA) ha arrancado.
		//
		// La "interfaz" es la parte física (el chip o módulo Wi-Fi) junto con el software 
		//que lo controla, que permite conectarse a una red Wi-Fi
		
		// Modo estación (STA) significa que el ESP32 se comporta como cliente,
		// intentando conectarse a un router o punto de acceso Wi-Fi
		//
		// Cuando se recibe este evento, el ESP32 intenta conectarse automáticamente
		// a la red Wi-Fi configurada previamente con esp_wifi_connect()
		//
		if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
			
		esp_wifi_connect();  // Inicia el proceso de conexión a la red Wi-Fi configurada previamente
		// Este método hace lo siguiente:
		// - Utiliza las credenciales (SSID y contraseña) que ya fueron configuradas y almacenadas en NVS.
		// - Inicia la búsqueda y negociación con el punto de acceso Wi-Fi (router).
		// - Gestiona el protocolo de autenticación y asociación con el router.
		// - Si la conexión es exitosa, el ESP32 recibirá una dirección IP (usualmente vía DHCP) y se disparará el evento IP_EVENT_STA_GOT_IP.
		
		// Se imprime un mensaje para informar del proceso
		    ESP_LOGI(TAG, "Wi-Fi STA iniciado, intentando conectar..."); 
		}

    // Si el evento es una desconexión de la red Wi-Fi...
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {  // Si no hemos alcanzado el número máximo de reintentos
            esp_wifi_connect();  // Intentamos reconectar
            s_retry_num++;  // Aumentamos el contador de reintentos
            ESP_LOGI(TAG, "Reintentando conexion Wi-Fi (%d/%d)", s_retry_num, WIFI_MAXIMUM_RETRY);  // Log informativo
        } else {  // Si ya se alcanzó el límite de reintentos
            ESP_LOGE(TAG, "No se pudo conectar despues de %d intentos", WIFI_MAXIMUM_RETRY);  // Log de error
            vTaskDelay(pdMS_TO_TICKS(1000));  // Esperamos 1 segundo antes de reiniciar
            ESP_ERROR_CHECK(nvs_flash_erase());  // Borramos la NVS para limpiar credenciales guardadas
            esp_restart();  // Reiniciamos el dispositivo para forzar reprovisionamiento 
        }
    }

    // Si el evento es que se obtuvo una IP válida desde el router (evento DHCP exitoso)...
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;  // Extraemos la IP desde la estructura del evento
        ESP_LOGI(TAG, "Conectado a la red Wi-Fi IP obtenida: " IPSTR, IP2STR(&event->ip_info.ip));  // Mostramos la IP obtenida
        s_retry_num = 0;  // Reiniciamos el contador de reintentos ya que la conexión fue exitosa
		
		// Inicia el gestor de conectividad, que internamente verifica
		//  y levanta todos los serviciones que necesita de internet
		conn_manager_init();  
    }
}



// -----------------------------------------------------------------------------
// Inicializa los sistemas básicos del ESP32 (memoria, red, Wi-Fi)
esp_err_t init_base_system(void)
{
    ESP_LOGI(TAG, "Inicializando NVS");
    esp_err_t ret = nvs_flash_init();                // Inicializa la memoria no volátil
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());          // Si hay error, borra la NVS
        ret = nvs_flash_init();                      // Y vuelve a inicializar
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Inicializando TCP/IP y event loop");
    ESP_ERROR_CHECK(esp_netif_init());               // Inicializa la pila de red
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // Inicializa el loop de eventos

    ESP_LOGI(TAG, "Creando interfaz Wi-Fi STA");
    /*
	 * La "interfaz de red" (esp_netif) es la parte que conecta el Wi-Fi del ESP32
	 * con el sistema que maneja el Internet (la pila TCP/IP)
	 
	 * Permite asignar direcciones IP, gestionar la configuración de red y enrutar los datos.
	 * Sin esta interfaz, el ESP32 no podría comunicarse correctamente a nivel de red.
	 */
	 
	 //Interfaz: Parte física/lógica que se conecta a la red	
    (void) esp_netif_create_default_wifi_sta();      // Crea la interfaz Wi-Fi en modo estación


    ESP_LOGI(TAG, "Inicializando driver Wi-Fi");
    /*
	 * El "driver de Wi-Fi" es el software que controla el hardware Wi-Fi del ESP32.
	 * Se encarga de gestionar la conexión, autenticación, transmisión y recepción de datos,
	 * así como la interacción con el sistema operativo y la pila de red.
	 * Sin este driver, el microcontrolador no podría comunicarse con redes Wi-Fi.
	 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // Configuración por defecto
    
    //Driver: Software que controla y gestiona la interfaz
    //El driver usa a la interfaz
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));            // Inicializa el driver Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); // Configura modo estación
	
	  // Registrar los manejadores (handler) antes de iniciar el Wi-Fi
	    
	 
    // Registra el mismo handler para todos los eventos Wi-Fi (inicio, desconexión, etc.)
    ESP_ERROR_CHECK(esp_event_handler_register(        // Verifica errores al registrar el handler
        WIFI_EVENT,                                    // Tipo de evento base (relacionado con Wi-Fi)
        ESP_EVENT_ANY_ID,                              // Se desea recibir cualquier tipo de evento Wi-Fi
        wifi_event_handler,                            // Función que manejará todos esos eventos
        NULL));                                        // Parámetro opcional (no se pasa nada extra)
 
	    
    // Registra el manejador para eventos IP: cuando el ESP32 obtiene una IP vía DHCP
    ESP_ERROR_CHECK(esp_event_handler_register(        // Verifica errores al registrar el handler
        IP_EVENT,                                      // Tipo de evento base (relacionado con IP)
        IP_EVENT_STA_GOT_IP,                           // Evento específico: se obtuvo una IP del router
        wifi_event_handler,                            // Función que manejará el evento
        NULL));                                        // Parámetro opcional (no se pasa nada extra)


    ESP_LOGI(TAG, "Iniciando Wi-Fi");
    
	// El evento WIFI_EVENT_STA_START es generado cuando se llama a esta funcion
	// indicando que el modo STA ha sido iniciado
	ESP_ERROR_CHECK(esp_wifi_start());  // Inicia el driver Wi-Fi en modo estación (STA)

    return ESP_OK;
}


// -----------------------------------------------------------------------------
// Handler de eventos del proceso de provisioning
// Cambia el estado del LED y maneja el flujo según el evento recibido
static void prov_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    switch (event_id) {
        case WIFI_PROV_START:                        // Cuando inicia el provisioning
            ESP_LOGI(TAG, "Provisioning iniciado");
            led_blink = true;                        // LED parpadea
            break;
        case WIFI_PROV_CRED_RECV: {                  // Cuando se reciben credenciales Wi-Fi
            wifi_sta_config_t *cfg = (wifi_sta_config_t *)event_data;
                
		    //wifi_sta_config_t es una estructura (struct) definida en el SDK de ESP-IDF que
		    // contiene la configuración para el modo estación (STA) de Wi-Fi
            ESP_LOGI(TAG, "Credenciales recibidas: SSID=%s", (char*)cfg->ssid);
            
            // En este punto se cargan las credenciales en el driver Wi-Fi para que el ESP32
		    // las use para conectarse y luego las guarda en NVS y las configura automáticamente

		    // una vez configuradas, puede llamar a esp_wifi_connect() para iniciar la conexión
		    // a la red Wi-Fi con esos datos recibidos
		    
            break;
        }
        case WIFI_PROV_CRED_SUCCESS:                 // Cuando el provisioning fue exitoso
            ESP_LOGI(TAG, "Provisioning exitoso");
            break;
        case WIFI_PROV_CRED_FAIL:                    // Cuando el provisioning falla
            ESP_LOGE(TAG, "Provisioning fallido");
            break;
        case WIFI_PROV_END:                          // Cuando termina el provisioning
            ESP_LOGI(TAG, "Provisioning finalizado, limpiando...");
            led_blink = false;                       // LED fijo encendido
            wifi_prov_mgr_deinit();                  // Libera recursos
            vTaskDelay(pdMS_TO_TICKS(100));          // Espera un poco
            esp_restart();                           // Reinicia el dispositivo
            break;
        default:
            break;
    }
}

// =============================================================================
// Handlers para Endpoints Personalizados
// =============================================================================

/**

 * @brief Manejador para procesar la configuración de base de datos (db-config) recibida vía protocolo personalizado.
 	brief: Es un tag que se usa para indicar una descripción de una función, variable o módulo, lo utilizan herramientas
 	de documentación automática
 
 * Este handler es llamado por el servidor de aprovisionamiento (prov) cuando se recibe un mensaje del tipo "db-config".
 * Extrae los campos `userId` y `espId` desde un JSON recibido en el buffer `inbuf`, y responde con "OK_DB_CONFIG".
 *
 * @param session_id ID de sesión actual (proporcionado por el servidor de aprovisionamiento)
 * @param inbuf      Buffer de entrada que contiene los datos recibidos (JSON)
 * @param inlen      Longitud de los datos en el buffer `inbuf`
 * @param outbuf     Puntero al buffer de salida que se enviará como respuesta
 * @param outlen     Longitud del buffer de salida
 * @param priv_data  Datos privados opcionales, no utilizados aquí
 *
 * @return ESP_OK en caso de éxito, o código de error de tipo esp_err_t
 */
static esp_err_t db_config_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    // Imprime información de depuración sobre la sesión y longitud de datos recibidos
    ESP_LOGI(TAG, "db-config handler recibido. Sesion ID: %" PRIu32 ", Longitud de datos: %zd", session_id, inlen);

    // Verifica que se hayan recibido datos válidos
    if (inbuf && inlen > 0) {
        // Reserva memoria para copiar los datos como cadena terminada en '\0' (necesaria para cJSON)
        char *json_str = (char *)malloc(inlen + 1);
        if (!json_str) {
            // Error de memoria al asignar buffer para el JSON
            ESP_LOGE(TAG, "Error: No hay memoria para json_str.");
            *outbuf = NULL;
            *outlen = 0;
            return ESP_ERR_NO_MEM;
        }

        // Copia los datos del buffer recibido y agrega terminador nulo
        memcpy(json_str, inbuf, inlen);
        json_str[inlen] = '\0';

        // Parsea el JSON recibido en un objeto cJSON
        cJSON *root = cJSON_Parse(json_str);
        if (root == NULL) {
            // Error al interpretar el JSON (formato inválido)
            ESP_LOGE(TAG, "Error al parsear JSON en db-config");
            free(json_str);
            *outbuf = NULL;
            *outlen = 0;
            return ESP_FAIL;
        }

        // Extrae el campo "userId" del JSON (esperado como cadena)
        cJSON *userId = cJSON_GetObjectItem(root, "userId");

        // Extrae el campo "espId" del JSON (esperado como cadena)
        cJSON *espId = cJSON_GetObjectItem(root, "espId");  // <-- NUEVO campo opcional para identificar el dispositivo

        // Verifica que userId exista y sea una cadena válida
        if (userId && cJSON_IsString(userId)) {
            ESP_LOGI(TAG, "User ID recibido: %s", userId->valuestring);
            // Aquí puedes guardar el userId en NVS o usarlo según tu lógica
        } else {
            ESP_LOGW(TAG, "User ID no recibido o inválido");
        }

        // Verifica que espId exista y sea una cadena válida
        if (espId && cJSON_IsString(espId)) {
            ESP_LOGI(TAG, "ESP ID recibido: %s", espId->valuestring);
            // Aquí también puedes guardar el espId si lo necesitas para MQTT u otra configuración
        } else {
            ESP_LOGW(TAG, "ESP ID no recibido o inválido");  
        }
        
        
        // Guardar userId en NVS bajo namespace "database" con key "user_id"
        if (userId && cJSON_IsString(userId)) {
            ESP_LOGI(TAG, "User ID recibido: %s", userId->valuestring);
            esp_err_t err = nvs_utils_save_blob("database", "user_id", userId->valuestring, strlen(userId->valuestring) + 1);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error guardando userId en NVS: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGW(TAG, "User ID no recibido o inválido");
        }

        // Guardar espId en NVS bajo namespace "dev_info" con key "esp_id"
        if (espId && cJSON_IsString(espId)) {
            ESP_LOGI(TAG, "ESP ID recibido: %s", espId->valuestring);
            esp_err_t err = nvs_utils_save_blob("dev_info", "esp_id", espId->valuestring, strlen(espId->valuestring) + 1);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error guardando espId en NVS: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGW(TAG, "ESP ID no recibido o inválido");
        }

        // Libera la memoria del JSON y el buffer temporal
        cJSON_Delete(root);
        free(json_str);
    } else {
        // Si no se recibió información en el buffer
        ESP_LOGW(TAG, "db-config handler recibido sin datos");
    }

    // Prepara una respuesta simple para confirmar recepción y parseo exitoso
    const char *response_str = "OK_DB_CONFIG";
    size_t response_len = strlen(response_str) + 1;  // +1 para incluir el terminador nulo

    // Asigna memoria para el buffer de salida
    *outbuf = (uint8_t *)malloc(response_len);
    if (*outbuf == NULL) {
        // Error al asignar memoria para respuesta
        ESP_LOGE(TAG, "Error de memoria al asignar outbuf para db-config");
        *outlen = 0;
        return ESP_ERR_NO_MEM;
    }

    // Copia la cadena de respuesta al buffer de salida
    memcpy(*outbuf, response_str, response_len);
    *outlen = response_len;

    // Devuelve ESP_OK para indicar éxito
    return ESP_OK;
}


/**
 * @brief Handler para procesar la configuración MQTT recibida en formato JSON.
 *
 * Esta función es llamada cuando se recibe un mensaje tipo "mqtt-config" desde el servidor de aprovisionamiento.
 * Extrae la URI del broker, username y password desde el JSON recibido, los muestra por log
 * y responde con un mensaje de confirmación.
 *
 * @param session_id ID de sesión de la comunicación actual (identificador único de sesión)
 * @param inbuf      Puntero al buffer de entrada que contiene los datos recibidos (en este caso un JSON)
 * @param inlen      Tamaño en bytes de los datos recibidos en inbuf
 * @param outbuf     Dirección del puntero que apuntará al buffer de salida para enviar respuesta
 * @param outlen     Dirección donde se almacenará la longitud del buffer de salida
 * @param priv_data  Puntero a datos privados (opcional y no usado en este handler)
 *
 * @return ESP_OK si el procesamiento fue exitoso, o un código de error esp_err_t en caso contrario
 */
static esp_err_t mqtt_config_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                     uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    // static: función con visibilidad solo dentro del archivo (scope interno)
    // esp_err_t: tipo de dato que representa códigos de error o éxito en ESP-IDF (int)
    // session_id: identificador numérico único para la sesión de aprovisionamiento
    // inbuf: buffer de datos recibidos (puntero constante a bytes)
    // inlen: tamaño del buffer inbuf
    // outbuf: puntero para asignar la respuesta que se enviará (puntero doble)
    // outlen: puntero para indicar el tamaño de la respuesta asignada
    // priv_data: datos adicionales, no usados aquí

    // Log informativo con el ID de sesión y tamaño de datos recibidos
    ESP_LOGI(TAG, "mqtt-config handler recibido. Sesion ID: %" PRIu32 ", Longitud de datos: %zd", session_id, inlen);

    // Verifica que se recibieron datos válidos (buffer no nulo y tamaño positivo)
    if (inbuf && inlen > 0) {
        // Reserva memoria dinámica para copiar los datos recibidos y agregar un terminador nulo '\0'
        // necesario para interpretar el buffer como una cadena de texto (string)
        char *json_str = (char *)malloc(inlen + 1);
        if (!json_str) {
            // Si no hay memoria suficiente, registra error y retorna código de error ESP_ERR_NO_MEM
            ESP_LOGE(TAG, "Error: No hay memoria para json_str.");
            *outbuf = NULL;  // No hay respuesta para enviar
            *outlen = 0;     // Longitud 0
            return ESP_ERR_NO_MEM;
        }

        // Copia el contenido de inbuf al buffer json_str
        memcpy(json_str, inbuf, inlen);

        // Añade el caracter nulo para terminar la cadena
        json_str[inlen] = '\0';

        // Parsea la cadena JSON con la librería cJSON, creando un árbol de objetos JSON
        cJSON *root = cJSON_Parse(json_str);
        if (root == NULL) {
            // Si no pudo parsear (JSON inválido), reporta error y limpia memoria
            ESP_LOGE(TAG, "Error al parsear JSON mqtt-config");
            free(json_str);
            *outbuf = NULL;
            *outlen = 0;
            return ESP_FAIL;  // Código genérico de fallo
        }

        // Extrae del JSON el campo "broker" (la URI del servidor MQTT)
        cJSON *broker = cJSON_GetObjectItem(root, "broker");

        // Extrae el campo "username" para la autenticación MQTT
        cJSON *username = cJSON_GetObjectItem(root, "username");

        // Extrae el campo "password" para la autenticación MQTT
        cJSON *password = cJSON_GetObjectItem(root, "password");
        
        mqtt_credentials_t creds = {0};  // Estructura para guardar credenciales


        // Verifica que el campo broker exista y sea una cadena válida
        if (broker && cJSON_IsString(broker)) {
            ESP_LOGI(TAG, "Broker URI: %s", broker->valuestring);
            
            // Establecemos el valor del broker al objeto para despues guardarlo 
		    strncpy(creds.broker, broker->valuestring, sizeof(creds.broker) - 1);
		    creds.broker[sizeof(creds.broker) - 1] = '\0';  // Seguridad extra
		    
        } else {
            ESP_LOGW(TAG, "Broker URI no recibido o inválido");
        }

        // Verifica que el campo username exista y sea cadena válida
        if (username && cJSON_IsString(username)) {
            ESP_LOGI(TAG, "MQTT Username: %s", username->valuestring);
            
            // Establecemos el valor del username al objeto para despues guardarlo 
		    strncpy(creds.username, username->valuestring, sizeof(creds.username) - 1);
		    creds.username[sizeof(creds.username) - 1] = '\0';
        } else {
            ESP_LOGW(TAG, "MQTT Username no recibido o inválido");
        }

        // Verifica que el campo password exista y sea cadena válida
        if (password && cJSON_IsString(password)) {
            ESP_LOGI(TAG, "MQTT Password: %s", password->valuestring);
            
            // Establecemos el valor del password al objeto para despues guardarlo 
		    strncpy(creds.password, password->valuestring, sizeof(creds.password) - 1);
		    creds.password[sizeof(creds.password) - 1] = '\0';
            
        } else {
            ESP_LOGW(TAG, "MQTT Password no recibido o inválido");
        }
        
         // Guardar las credenciales en NVS
        ESP_LOGI(TAG, "Guardando credenciales MQTT: broker=%s, usuario=%s", creds.broker, creds.username);
        esp_err_t err = mqtt_credentials_save(&creds);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error guardando credenciales MQTT en NVS: %s", esp_err_to_name(err));
            cJSON_Delete(root);
            free(json_str);
            *outbuf = NULL;
            *outlen = 0;
            return err;
        } else {
            ESP_LOGI(TAG, "Credenciales MQTT guardadas correctamente en NVS");
        }
        
        // Libera el árbol JSON para evitar fugas de memoria
        cJSON_Delete(root);

        // Libera el buffer temporal de la cadena JSON
        free(json_str);
    } else {
        // Si no se recibieron datos, muestra una advertencia en el log
        ESP_LOGW(TAG, "mqtt-config handler recibido sin datos");
    }

    // Prepara una cadena de respuesta para confirmar la recepción exitosa del mensaje
    const char *response_str = "OK_MQTT_CONFIG";

    // Calcula la longitud de la respuesta incluyendo el terminador nulo
    size_t response_len = strlen(response_str) + 1;

    // Reserva memoria para el buffer de salida (respuesta)
    *outbuf = (uint8_t *)malloc(response_len);
    if (*outbuf == NULL) {
        // Error si no pudo asignar memoria para la respuesta
        ESP_LOGE(TAG, "Error de memoria al asignar respuesta mqtt-config");
        *outlen = 0;
        return ESP_ERR_NO_MEM;
    }

    // Copia la respuesta al buffer de salida
    memcpy(*outbuf, response_str, response_len);

    // Establece la longitud del buffer de salida
    *outlen = response_len;

    // Retorna ESP_OK para indicar que el handler ejecutó correctamente
    return ESP_OK;
}


static esp_err_t ping_conn_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    ESP_LOGI(TAG, "Ping recibido en el endpoint 'ping-conn'. Sesion ID: %" PRIu32, session_id);

    const char *response_str = "PONG";
    size_t response_len = strlen(response_str) + 1;

    *outbuf = (uint8_t *)malloc(response_len);
    if (*outbuf == NULL) {
        *outlen = 0;
        return ESP_ERR_NO_MEM;
    }
    memcpy(*outbuf, response_str, response_len);
    *outlen = response_len;
    return ESP_OK;
}



// -----------------------------------------------------------------------------
// Función principal que inicia todo el proceso de provisionamiento Wi-Fi
void my_wifi_prov_startup(void)
{
    init_led_and_button(); // Inicializa los pines del LED y el botón de reprovisionamiento (configura los GPIOs).

    xTaskCreate(led_task, "led_task", 2048, NULL, 5, &led_task_handle); 
    // Crea la tarea (task) que controla el LED. Esta tarea se ejecuta en paralelo y hace que el LED parpadee o permanezca encendido según el estado.

    xTaskCreate(reprov_button_task, "reprov_button_task", 2048, NULL, 5, NULL); 
    // Crea la tarea que monitorea el botón de reprovisionamiento. Si el usuario mantiene presionado el botón 3 segundos, borra la configuración y reinicia el dispositivo.

    ESP_ERROR_CHECK(init_base_system()); 
    // Inicializa los sistemas base del ESP32: memoria no volátil (NVS), red (TCP/IP), Wi-Fi y event loop.
    ESP_LOGI(TAG, "Sistema base listo, iniciando provisionamiento");
    // Muestra en consola que la inicialización básica fue exitosa y se procederá al provisioning.

	        

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
        prov_event_handler, NULL)); 
    // Registra la función que manejará los eventos del proceso de provisionamiento (inicio, éxito, fallo, fin)
    
  

    bool provisioned = false; // Variable para saber si el dispositivo ya fue configurado anteriormente
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned)); // Consulta al manager de provisioning si el dispositivo ya tiene credenciales Wi-Fi guardadas
    provisioned_global = provisioned; 
    // Guarda el estado globalmente para otras partes del programa.

    if (provisioned) {
        led_blink = false; // Si ya está configurado, el LED queda fijo encendido
        // El sistema sigue funcionando normalmente, la tarea reprov_button_task se encarga del reprovisionamiento si el usuario lo solicita
   		
   	} else {
        led_blink = true; // Si NO está configurado, el LED parpadea indicando que está esperando ser provisionado

        ESP_ERROR_CHECK(my_wifi_prov_mgr_init()); 
        // Inicializa el manager de provisioning con el esquema seleccionado (BLE o SoftAP).
        ESP_LOGI(TAG, "Manager de provisioning inicializado");
        // Muestra en consola que el manager de provisioning está listo.
        
		// Crea el endpoint (punto de conexión) para recibir configuración de la base de datos
		// Este endpoint escuchará mensajes tipo "db-config" y llamará al handler asociado
		wifi_prov_mgr_endpoint_create("db-config");
		
		// Crea el endpoint para recibir configuración MQTT
		// Este endpoint escuchará mensajes tipo "mqtt-config" y llamará al handler correspondiente
		wifi_prov_mgr_endpoint_create("mqtt-config");
		
		// Crea el endpoint para manejar mensajes de ping de conexión
		// Útil para comprobar que el dispositivo sigue conectado y responde
		wifi_prov_mgr_endpoint_create("ping-conn");

        ESP_LOGI(TAG, "Endpoints personalizados 'db-config','mqtt-config' y ping-conn creados.");

        wifi_prov_security2_params_t sec2_params = { // Estructura con los parámetros de seguridad para el esquema SRP6a (Security 2)
            .salt = sec2_salt,                      // Salt único para este dispositivo (protege la contraseña)
            .salt_len = sec2_salt_len,              // Longitud del salt
            .verifier = sec2_verifier,              // Verificador calculado a partir de la contraseña y el salt
            .verifier_len = sec2_verifier_len       // Longitud del verificador
        };

        const char *service_name = "ReptiTrack_BLE"; // Nombre del servicio BLE que verá el usuario en la app móvil
        //const char *username = EXAMPLE_PROV_SEC2_USERNAME; // (opcional) Usuario para el provisioning

        // Inicia el proceso de provisioning BLE con seguridad SRP6a, usando los parámetros definidos y el nombre de servicio.
        ESP_ERROR_CHECK(my_wifi_prov_mgr_start(
            WIFI_PROV_SECURITY_2, &sec2_params,
            service_name, NULL)); 
        ESP_LOGI(TAG, "Provisioning BLE iniciado (nombre: %s)", service_name); // Muestra en consola que el provisioning BLE ha comenzado.

		
        // --- INICIO: Agrega este bloque para registrar los handlers ---
        // Los handlers se registran *después* de my_wifi_prov_mgr_start()
        ESP_ERROR_CHECK(wifi_prov_mgr_endpoint_register("db-config", db_config_handler, NULL));
        ESP_LOGI(TAG, "Handler para 'db-config' registrado.");

        ESP_ERROR_CHECK(wifi_prov_mgr_endpoint_register("mqtt-config", mqtt_config_handler, NULL));
        ESP_LOGI(TAG, "Handler para 'mqtt-config' registrado.");
        
        ESP_ERROR_CHECK( wifi_prov_mgr_endpoint_register("ping-conn", ping_conn_handler, NULL));
        ESP_LOGI(TAG, "Handler para 'ping-conn' registrado.");

        
       // Codigo para generar el codigo QR
       		
		//const char *username  = EXAMPLE_PROV_SEC2_USERNAME;
		//const char *password = EXAMPLE_PROV_SEC2_PWD; 
      	//  my_wifi_prov_print_qr(service_name, username, password,PROV_TRANSPORT_BLE); 
        // Imprime en consola el QR para que la app móvil pueda escanearlo y conectarse fácilmente al dispositivo.
        // Usa NULL en lugar de la contraseña porque el protocolo SRP6a usa el verificador (sec2_verifier) para mayor seguridad.
    }
}

/*
 * RESUMEN DE FUNCIONES:
 * - init_led_and_button: Configura los pines del LED y el botón de reprovisionamiento.
 * - led_task: Tarea que controla el parpadeo o encendido fijo del LED según el estado del provisioning.
 * - wait_button_3s: Espera a que el botón esté presionado 3 segundos para activar el reprovisionamiento.
 * - reprov_button_task: Tarea que detecta si el usuario quiere reprovisionar y reinicia el dispositivo si es necesario.
 * - my_wifi_prov_mgr_init: Inicializa el manager de provisioning Wi-Fi con el esquema seleccionado (BLE o SoftAP).
 * - my_wifi_prov_mgr_start: Inicia el proceso de provisioning con los parámetros de seguridad y nombre de servicio.
 * - my_wifi_prov_mgr_deinit: Libera los recursos usados por el provisioning.
 * - my_wifi_prov_print_qr: Imprime en consola el QR para la app de Espressif.
 * - init_base_system: Inicializa la memoria, red y Wi-Fi del ESP32.
 * - prov_event_handler: Maneja los eventos del proceso de provisioning (inicio, éxito, fallo, fin).
 * - my_wifi_prov_startup: Función principal que orquesta todo el proceso de provisioning Wi-Fi.
 */