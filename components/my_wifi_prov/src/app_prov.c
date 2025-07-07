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

static const char *TAG = "wifi_prov";             // Etiqueta para los mensajes en consola

static TaskHandle_t led_task_handle = NULL;       // Identificador de la tarea del LED
static bool led_blink = false;                    // Indica si el LED debe parpadear o quedarse fijo
static bool provisioned_global = false;           // Indica si el dispositivo ya está configurado

#define EXAMPLE_PROV_SEC2_USERNAME          "reptitrack"
//#define EXAMPLE_PROV_SEC2_PWD               "xp4tzq7" // (opcional) Contraseña para el provisionamiento

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
            ESP_LOGI(TAG, "Botón presionado 3s. Borrando NVS y reiniciando para reprovisionar.");
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
    ESP_LOGI(TAG, "Imprimiendo QR para provision");

    char payload[256]; // Buffer para el JSON del QR
#if CONFIG_EXAMPLE_PROV_SECURITY_VERSION_2
    snprintf(payload, sizeof(payload),
             "{\"ver\":\"v1\",\"name\":\"%s\",\"username\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
             service_name, username, pop, transport);
#else
    snprintf(payload, sizeof(payload),
             "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
             service_name, pop, transport);
#endif

    printf("Escanea este codigo QR en la app de provision:\n\n");
    printf("https://espressif.github.io/esp-jumpstart/qrcode.html?data=%s\n\n", payload);
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
    (void) esp_netif_create_default_wifi_sta();      // Crea la interfaz Wi-Fi en modo estación

    ESP_LOGI(TAG, "Inicializando driver Wi-Fi");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // Configuración por defecto
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));            // Inicializa el driver Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); // Configura modo estación

    ESP_LOGI(TAG, "Iniciando Wi-Fi");
    ESP_ERROR_CHECK(esp_wifi_start());               // Inicia el Wi-Fi

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
            ESP_LOGI(TAG, "Credenciales recibidas: SSID=%s", (char*)cfg->ssid);
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


void print_current_wifi_info(void) {
    wifi_config_t wifi_config;
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK) {
        ESP_LOGI(TAG, "Ya estas conectado a la red: %s", (char *)wifi_config.sta.ssid);
    } else {
        ESP_LOGW(TAG, "No se pudo obtener la configuracion Wi-Fi");
    }
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
	     print_current_wifi_info(); // funcion para mostrar a que red esta conectado el dispositivo
    } else {
        led_blink = true; // Si NO está configurado, el LED parpadea indicando que está esperando ser provisionado

        ESP_ERROR_CHECK(my_wifi_prov_mgr_init()); 
        // Inicializa el manager de provisioning con el esquema seleccionado (BLE o SoftAP).
        ESP_LOGI(TAG, "Manager de provisioning inicializado");
        // Muestra en consola que el manager de provisioning está listo.

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


        //my_wifi_prov_print_qr(service_name, username, NULL, "ble"); 
        // (Opcional) Imprime en consola el QR para que la app móvil pueda escanearlo y conectarse fácilmente al dispositivo.
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