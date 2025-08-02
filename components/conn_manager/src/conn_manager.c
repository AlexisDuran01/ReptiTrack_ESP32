#include "conn_manager.h"         // Incluye el archivo de cabecera público del gestor de conectividad.
#include "esp_log.h"              // Incluye funciones para logging de ESP-IDF (ESP_LOGI, ESP_LOGW, ESP_LOGE).
#include "freertos/FreeRTOS.h"    // Incluye definiciones básicas de FreeRTOS (tipos, macros).
#include "freertos/task.h"        // Incluye funciones para manejo de tareas FreeRTOS (xTaskCreate, vTaskDelete).
#include "network_utils.h"        // Incluye funciones utilitarias de red, como check_internet_connection(), get_local_ip(), etc.
#include "rtc_time.h"             // Incluye funciones para sincronización y manejo del reloj RTC.
#include "mqtt_cliente.h"
#include "sensors_manager.h"
#include "firestore.h"
#include "actuators_manager.h"

static const char *TAG = "conn_manager";  
// TAG para usar en logs con ESP_LOG*, para identificar la fuente del mensaje en la consola.
// Ejemplo: [conn_manager] INFO: mensaje de log

// Instancia singleton simple usando puntero a variable estática única
static void *s_instance = NULL;
// Puntero estático privado que almacena la instancia única (singleton) del gestor de conectividad.
// Está inicializado en NULL, indicando que aún no se creó la instancia.
// Esta variable es interna a este archivo (static), no accesible desde otros módulos.

static bool s_initialized = false;
// Variable de estado que indica si el gestor ya fue inicializado (tipo booleano).

// Tarea que se ejecuta después de obtener IP válida (post Wi-Fi)
// Esta función es la tarea FreeRTOS que se lanza tras confirmar que el dispositivo tiene una IP válida.
// Se encarga de imprimir información de red y sincronizar la hora, entre otras posibles acciones.
static void wifi_services_init_task(void *pvParameter) {
    ESP_LOGI(TAG, "Iniciando verificacion post Wi-Fi...");  
    // Log informativo indicando que se inició la tarea post Wi-Fi

    char ip_str[16] = {0};  // Buffer para almacenar la IP local como cadena (ej. "192.168.1.10")
    char gw_str[16] = {0};  // Buffer para almacenar la IP del gateway
    char dns_str[16] = {0}; // Buffer para almacenar la IP del servidor DNS

    // Obtiene y muestra la IP local usando función utilitaria
    if (get_local_ip(ip_str, sizeof(ip_str)) == ESP_OK) {
        ESP_LOGI(TAG, "IP local: %s", ip_str);
    } else {
        ESP_LOGW(TAG, "No se pudo obtener la IP local");
    }

    // Obtiene y muestra la IP del gateway (puerta de enlace)
    if (get_gateway_ip(gw_str, sizeof(gw_str)) == ESP_OK) {
        ESP_LOGI(TAG, "Gateway: %s", gw_str);
    } else {
        ESP_LOGW(TAG, "No se pudo obtener el gateway");
    }

    // Obtiene y muestra la IP del servidor DNS principal
    if (get_dns_ip(dns_str, sizeof(dns_str)) == ESP_OK) {
        ESP_LOGI(TAG, "DNS: %s", dns_str);
    } else {
        ESP_LOGW(TAG, "No se pudo obtener el DNS");
    }

    // Comprueba la conexión a internet con un timeout de 3000 ms (3 segundos)
    if (check_internet_connection(3000)) {
        ESP_LOGI(TAG, "Conexion a internet OK");
        rtc_time_sync_with_timezone("America/Mexico_City"); // Sincroniza la hora local con el servidor NTP
        rtc_time_print_current();       
        

      	 // Inicializar MQTT aquí:
		mqtt_cliente_init();
		 
        firestore_init();
	        
	    actuators_manager_init();
	    
		actuators_manager_start_all();
			
        

		char *response = NULL;
		size_t len = 0;
		
		if (firestore_is_initialized()) {
		    esp_err_t err = firestore_get_document("terrarios", &response, &len);
		    if (err == ESP_OK) {
		        ESP_LOGI(TAG, "Respuesta Firestore (%d bytes): %s\n", (int)len, response);
		        free(response);  // Liberar memoria para evitar fugas
		    } else {
		        ESP_LOGE(TAG, "Error leyendo documento Firestore: %s", esp_err_to_name(err));
		    }
		} else {
		    ESP_LOGE(TAG, "Firestore no inicializado correctamente");
		}


	   		 
    } else {
        ESP_LOGW(TAG, "No hay conexión a internet, no se puede sincronizar la hora ni verificar servicios");
    }

	
	/*
		Cuando se pasa NULL como parámetro, FreeRTOS interpreta que debe eliminar la 
		tarea que está ejecutando actualmente (es decir, la tarea que llamó 
		a vTaskDelete(NULL))
	*/
    vTaskDelete(NULL); // Finaliza la tarea actual y libera los recursos asignados por FreeRTOS
}


/// @brief Devuelve (y crea si es necesario) la instancia singleton del gestor de conectividad.
/// @return Puntero genérico a la instancia 
void *conn_manager_get_instance(void) {
    if (!s_initialized) {
        s_initialized = true;
        s_instance = (void*)0x1; // Asignamos un valor no nulo cualquiera para representar la instancia
        ESP_LOGD(TAG, "Instancia del gestor de conectividad creada");
    }
    return s_instance;
	}
	
	
	/// Inicializa el gestor de conectividad
void conn_manager_init(void) {
    // Verifica si la instancia singleton aún no está creada
    if (s_instance == NULL) {
        // Obtiene (y crea si no existe) la instancia singleton del gestor
        conn_manager_get_instance();

        // Log informativo que indica que el gestor fue inicializado correctamente
        ESP_LOGI(TAG, "Gestor de conectividad inicializado");

        // Crea la tarea FreeRTOS para realizar acciones post Wi-Fi,
        // como verificar conectividad, sincronizar hora, etc.
        BaseType_t result = xTaskCreate(
            wifi_services_init_task, // Función que ejecuta la tarea
            "wifi_services_init",             // Nombre identificativo de la tarea
            8192,                         // Tamaño de pila asignado a la tarea (en bytes)
            NULL,                         // Parámetro que se pasa a la tarea (aquí ninguno)
            5,                            // Prioridad de la tarea (5 es valor medio-alto)
            NULL                          // Puntero para almacenar el handle de la tarea (no se usa aquí)
        );

        // Comprueba si la creación de la tarea fue exitosa
        if (result != pdPASS) {
            // Si falla, imprime error en el log
            ESP_LOGE(TAG, "Error al crear tarea post Wi-Fi");
        }

    } else {
        // Si la instancia ya existe, imprime advertencia para evitar re-inicialización
        ESP_LOGW(TAG, "Gestor de conectividad ya inicializado");
    }
}


/// Detiene el gestor de conectividad y libera recursos
void conn_manager_deinit(void) {
    // Aquí podrías agregar la limpieza, si es que creas tareas que requieren ser eliminadas,
    // cerrar conexiones, liberar memoria, etc.

    s_instance = NULL;
    ESP_LOGI(TAG, "Gestor de conectividad detenido y recursos liberados");
}
