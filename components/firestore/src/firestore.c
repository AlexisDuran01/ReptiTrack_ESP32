#include "firestore.h"
#include "nvs_utils.h"
#include "esp_log.h"
#include <string.h>
#include "esp_crt_bundle.h"  
#include "esp_http_client.h"
#include "cJSON.h"  // Asegúrate de incluir cJSON si no está

static const char *TAG = "firestore";

#define FIRESTORE_PROJECT_ID "reptitrack-946e0"
#define FIRESTORE_BASE_URL_FORMAT "https://firestore.googleapis.com/v1/projects/%s/databases/(default)/documents/usuarios/%s"


// Buffer interno donde se almacena la URL base construida
static char firestore_base_url[FIRESTORE_BASE_URL_MAX_LEN] = {0};
static bool firestore_initialized = false;

// Estructura para manejar la respuesta HTTP de forma dinámica
typedef struct {
    char *buffer;     // Apunta al contenido recibido (en memoria dinámica)
    size_t length;    // Cantidad actual de bytes leídos en el buffer
    size_t capacity;  // Capacidad total del buffer asignado
} http_response_t;


// Manejador de eventos del cliente HTTP
static esp_err_t client_event_handler(esp_http_client_event_t *evt) {
    // Se convierte el puntero genérico de datos del evento a nuestra estructura http_response_t
    http_response_t *resp = (http_response_t *)evt->user_data;

    // Se evalúa el tipo de evento recibido
    switch (evt->event_id) {
		
	// Se ejecuta si ocurre un error en cualquier parte del proceso HTTP
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP_EVENT_ERROR: Ocurrio un error en la conexion o solicitud");
        break;


	
    // Se ejecuta justo después de que se establece una conexión con el servidor
    case HTTP_EVENT_ON_CONNECTED:
        //ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED: Conexion TLS establecida con el servidor");
        break;

	 // Se ejecuta cada vez que se reciben datos del cuerpo (body) de la respuesta.
    // Puede llamarse varias veces si la respuesta es grande o fragmentada
    
    // Evento cuando se recibe un fragmento de datos (puede ser llamado múltiples veces)
    case HTTP_EVENT_ON_DATA:

        // Verificamos si hay suficiente espacio para copiar los nuevos datos (+1 para el '\0')
        if (resp->length + evt->data_len + 1 > resp->capacity) {
            // Se calcula una nueva capacidad: se duplica la actual y se suma lo nuevo
            size_t new_capacity = resp->capacity * 2 + evt->data_len;

            // Se intenta reservar más memoria con realloc
            char *new_buf = realloc(resp->buffer, new_capacity);
            if (!new_buf) {
                // Si falla realloc, se reporta error de memoria
                ESP_LOGE(TAG, "No hay memoria para ampliar buffer");
                return ESP_FAIL;
            }

            // Se actualiza el buffer y la capacidad
            resp->buffer = new_buf;
            resp->capacity = new_capacity;
        }

        // Se copian los nuevos datos al final del buffer existente
        memcpy(resp->buffer + resp->length, evt->data, evt->data_len);

        // Se actualiza el tamaño total actual de datos en el buffer
        resp->length += evt->data_len;

        // Se agrega carácter nulo al final para mantenerlo como string C válido
        resp->buffer[resp->length] = '\0';

        break;

	    // Se ejecuta cuando se ha terminado de recibir toda la respuesta del servidor
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH: Peticion finalizada (respuesta completa)");
        break;
        
      //  Se ejecuta si se pierde o se cierra la conexión con el servidor (antes o después de enviar/recibir datos)
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "HTTP_EVENT_DISCONNECTED: Conexion cerrada o perdida");
        break;
        
    // Evento no manejado explícitamente, se muestra advertencia por si se agregan nuevos en el futuro
    default:
        break;
    }
    
    return ESP_OK; // Indica que el evento fue procesado correctamente
}



// Variable global o estática para valor de prueba
//static char firestore_test_terrario_id[64] = "GxjEjgvAljSA7nvxNQUU";

/// @brief Inicializa cualquier estado interno de Firestore (por ahora no hace nada)
void firestore_init(void) {
    if (!firestore_build_base_url()) {
        ESP_LOGE(TAG, "No se pudo inicializar Firestore: no se pudo construir URL base");
        firestore_initialized = false;
    } else {
        ESP_LOGI(TAG, "Firestore inicializado correctamente");
        firestore_initialized = true;
    }
}


/// @brief Construye la URL base para Firestore usando los IDs almacenados en NVS
// Construye la URL base para Firestore con los datos cargados
bool firestore_build_base_url(void) {
    firestore_t data = {0};
    esp_err_t err = firestore_load(&data);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error cargando datos Firestore para URL base: %s", esp_err_to_name(err));
        return false;
    }

    if (strlen(data.user_id) == 0) {
        ESP_LOGE(TAG, "Falta user_id");
        return false;
    }


    int written = snprintf(firestore_base_url, sizeof(firestore_base_url),
                           FIRESTORE_BASE_URL_FORMAT,
                           FIRESTORE_PROJECT_ID,
                           data.user_id);

    if (written <= 0 || written >= sizeof(firestore_base_url)) {
        ESP_LOGE(TAG, "Error construyendo URL base (buffer insuficiente)");
        firestore_base_url[0] = '\0';
        return false;
    }

    ESP_LOGI(TAG, "URL base construida: %s", firestore_base_url);
    return true;
}


char *firestore_build_url(const char *subpath) {
    if (!firestore_is_initialized()) {
        ESP_LOGE(TAG, "Firestore no inicializado");
        return NULL;
    }

    const char *base_url = firestore_get_base_url(); // Debe haber sido construida antes
    if (!base_url || !subpath) {
        ESP_LOGE(TAG, "Parámetros inválidos");
        return NULL;
    }

    // Evita doble slash: si subpath inicia con '/', lo ignoramos
    const char *clean_subpath = (subpath[0] == '/') ? subpath + 1 : subpath;

    size_t total_len = strlen(base_url) + 1 + strlen(clean_subpath) + 1; // +1 por '/', +1 por '\0'
    char *full_url = malloc(total_len);
    if (!full_url) {
        ESP_LOGE(TAG, "No se pudo asignar memoria para full_url");
        return NULL;
    }

    snprintf(full_url, total_len, "%s/%s", base_url, clean_subpath);
    return full_url;
}

/// @brief Libera recursos internos (por ahora no hace nada)
void firestore_deinit(void) {
    // Si hay recursos dinámicos, liberar aquí
}


bool firestore_is_initialized(void) {
    return firestore_initialized;
}


bool firestore_check_connectivity(void) {
    if (!firestore_is_initialized()) {
        ESP_LOGW(TAG, "Firestore no inicializado");
        return false;
    }

    const char *test_path = "terrarios";
    char *response = NULL;
    size_t response_len = 0;

    esp_err_t err = firestore_get_document(test_path, &response, &response_len);
    if (err != ESP_OK || !response) {
        ESP_LOGE(TAG, "Fallo al hacer GET para verificar conectividad");
        return false;
    }

    cJSON *json = cJSON_Parse(response);
    if (!json) {
        ESP_LOGE(TAG, "Respuesta no es JSON válido");
        free(response);
        return false;
    }

    cJSON *documents = cJSON_GetObjectItem(json, "documents");
    bool resultado = false;

    if (documents && cJSON_IsArray(documents) && cJSON_GetArraySize(documents) > 0) {
        cJSON *primer_doc = cJSON_GetArrayItem(documents, 0);
        cJSON *name_field = cJSON_GetObjectItem(primer_doc, "name");

        if (name_field && cJSON_IsString(name_field)) {
            const char *full_name = name_field->valuestring;

            // Extraer el ID final del path completo (último segmento del path)
            const char *last_slash = strrchr(full_name, '/');
            if (last_slash && *(last_slash + 1)) {
                const char *terrario_id = last_slash + 1;
                ESP_LOGI(TAG, "ID de terrario encontrado: %s", terrario_id);

                // Guardar el ID como blob en NVS
                esp_err_t save_err = nvs_utils_save_blob("terrarios", "id", terrario_id, strlen(terrario_id) + 1);
                if (save_err == ESP_OK) {
                    resultado = true;
                } else {
                    ESP_LOGE(TAG, "Error al guardar terrario_id en NVS: %s", esp_err_to_name(save_err));
                }
            } else {
                ESP_LOGW(TAG, "No se pudo extraer ID del campo name");
            }
        } else {
        }
    } else {
        ESP_LOGW(TAG, "No hay documentos en la colección terrarios");
    }

    cJSON_Delete(json);
    free(response);
    return resultado;
}



/// @brief Retorna la URL base construida anteriormente
// Retorna la URL base construida (o NULL si no está construida)
const char *firestore_get_base_url(void) {
    return (strlen(firestore_base_url) > 0) ? firestore_base_url : NULL;
}
//
// -----------------------------
// GUARDAR / CARGAR CREDENCIALES (NVS)
// -----------------------------

/// @brief Guarda los IDs de Firestore (user_id y terrario_id) en NVS
/// @param data Puntero a la estructura firestore_t con los datos a guardar
/// @return ESP_OK si se guardó correctamente, o un código de error si falló
esp_err_t firestore_save(const firestore_t *data) {
    if (!data) return ESP_ERR_INVALID_ARG;

    // Guardar estructura como blob binario usando namespace y clave definidos
    return nvs_utils_save_blob(NVS_NAMESPACE_FIRESTORE, NVS_KEY_FIRESTORE, data, sizeof(firestore_t));
}

/// @brief Carga los IDs de Firestore (user_id y terrario_id) desde NVS
/// @param data Puntero a estructura firestore_t donde se almacenarán los datos cargados
/// @return ESP_OK si la carga fue exitosa, o un código de error si falló
esp_err_t firestore_load(firestore_t *data) {
    if (!data) return ESP_ERR_INVALID_ARG;

    size_t actual_len = 0;

    // Leer estructura desde NVS y validar tamaño
    esp_err_t err = nvs_utils_load_blob(NVS_NAMESPACE_FIRESTORE, NVS_KEY_FIRESTORE, data, sizeof(firestore_t), &actual_len);

    if (err == ESP_OK && actual_len != sizeof(firestore_t)) {
        ESP_LOGW(TAG, "Tamaño inesperado al cargar Firestore data (%zu bytes)", actual_len);
        return ESP_ERR_INVALID_SIZE;
    }

    return err;
}



// Función para realizar una solicitud GET a Firestore con un subpath dado.
// Devuelve la respuesta en un buffer dinámico que el llamador debe liberar.
esp_err_t firestore_get_document(const char *subpath, char **outResponse, size_t *out_len) {

    // Verifica si Firestore fue inicializado correctamente
    if (!firestore_is_initialized()) return ESP_ERR_INVALID_STATE;

    // Verifica que los punteros de entrada no sean nulos
    if (!subpath || !outResponse || !out_len) return ESP_ERR_INVALID_ARG;

    // Construye la URL completa usando el subpath dado (ej. "terrarios")
    char *full_url = firestore_build_url(subpath);
    if (!full_url) return ESP_FAIL;

    // Inicializa estructura para acumular la respuesta HTTP
    http_response_t response = {
        .buffer = malloc(2048),    // Reserva 512 bytes inicialmente
        .length = 0,              // Aún no hay datos
        .capacity = 2048           // Capacidad inicial
    };

    // Verifica si hubo éxito al reservar memoria
    if (!response.buffer) {
        free(full_url);           // Libera la URL si falla
        return ESP_ERR_NO_MEM;
    }

    // Inicializa el contenido del buffer con string vacío
    response.buffer[0] = '\0';

    // Configuración del cliente HTTP para realizar la petición GET
    esp_http_client_config_t config = {
        .url = full_url,                      // URL completa
        .method = HTTP_METHOD_GET,            // Método HTTP GET
        .transport_type = HTTP_TRANSPORT_OVER_SSL,  // HTTPS
        .crt_bundle_attach = esp_crt_bundle_attach, // Certificados raíz
        .timeout_ms = 5000,                   // Tiempo de espera (5 segundos)
        .event_handler = client_event_handler,      // Manejador de eventos HTTP
        .user_data = &response,               // Pasamos la estructura para acumular la respuesta
    };
    
/*
		Le pasamos la función client_event_handler como manejador de eventos al cliente HTTP.  
		A partir de ahí, el cliente se encargará de invocar automáticamente esta función  
		cada vez que ocurra un evento relevante durante el ciclo de vida de la petición HTTP,  
		como recibir datos, encabezados, errores, etc.
		*/

    // Inicializa el cliente HTTP con la configuración
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(response.buffer);  // Libera memoria si falla
        free(full_url);
        return ESP_FAIL;
    }

    // Realiza la solicitud HTTP sincrónicamente
    esp_err_t err = esp_http_client_perform(client);

    // Obtiene el código de estado HTTP (ej. 200 OK, 404, etc.)
    int status = esp_http_client_get_status_code(client);

    // Limpia y destruye el cliente HTTP
    esp_http_client_cleanup(client);
    free(full_url); // Libera memoria de la URL

    // Verifica si hubo error en la petición o si el código no fue 200
    if (err != ESP_OK || status != 200) {
        free(response.buffer); // Libera memoria del buffer si hubo error
        return ESP_FAIL;
    }

    // Asigna la respuesta y su longitud a los punteros de salida
    *outResponse = response.buffer;
    *out_len = response.length;

    // Retorna éxito
    return ESP_OK;
}


// Función para crear un nuevo documento en Firestore usando una solicitud HTTP POST.
// Envía el contenido JSON indicado en `jsonPayload` al path especificado.
// La respuesta del servidor (si se desea) se almacena en `outResponse` hasta un máximo de `max_len`.
// El llamador debe proporcionar el buffer `outResponse` ya reservado.
esp_err_t firestore_create_document(const char *subpath, const char *jsonPayload, char *outResponse, size_t max_len) {
    if (!firestore_is_initialized()) return ESP_ERR_INVALID_STATE;
    if (!subpath || !jsonPayload || !outResponse || max_len == 0) return ESP_ERR_INVALID_ARG;

    char *full_url = firestore_build_url(subpath);
    if (!full_url) return ESP_FAIL;

    http_response_t response = {
        .buffer = malloc(2048),
        .length = 0,
        .capacity = 2048
    };

    if (!response.buffer) {
        free(full_url);
        return ESP_ERR_NO_MEM;
    }

    response.buffer[0] = '\0';

    esp_http_client_config_t config = {
        .url = full_url,
        .method = HTTP_METHOD_POST,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
        .event_handler = client_event_handler,
        .user_data = &response,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(response.buffer);
        free(full_url);
        return ESP_FAIL;
    }

    // Headers obligatorios
    esp_http_client_set_header(client, "Content-Type", "application/json");

    // Asignar cuerpo JSON de la solicitud
    esp_http_client_set_post_field(client, jsonPayload, strlen(jsonPayload));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);
    free(full_url);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "Error al crear documento. Código HTTP: %d", status);
        free(response.buffer);
        return ESP_FAIL;
    }

    // Copiar respuesta al buffer proporcionado por el usuario
    if (response.length >= max_len) {
        ESP_LOGW(TAG, "Respuesta demasiado larga (%zu bytes), se truncará a %zu bytes", response.length, max_len - 1);
        response.buffer[max_len - 1] = '\0';
        strncpy(outResponse, response.buffer, max_len - 1);
        outResponse[max_len - 1] = '\0';
    } else {
        strcpy(outResponse, response.buffer);
    }

    free(response.buffer);
    return ESP_OK;
}



esp_err_t firestore_overwrite_document(const char *subpath, const char *jsonPayload, char *outResponse, size_t max_len) {
    if (!firestore_is_initialized()) return ESP_ERR_INVALID_STATE;
    if (!subpath || !jsonPayload || !outResponse || max_len == 0) return ESP_ERR_INVALID_ARG;

    // Construir URL base con helper
    char *url_base = firestore_build_url(subpath);
    if (!url_base) return ESP_ERR_NO_MEM;

    // En este caso no hay query, solo necesitamos la URL completa tal cual
    char *full_url = strdup(url_base);
    free(url_base);
    if (!full_url) return ESP_ERR_NO_MEM;

    // Preparar buffer para respuesta
    http_response_t response = {
        .buffer = malloc(2048),
        .length = 0,
        .capacity = 2048
    };
    if (!response.buffer) {
        free(full_url);
        return ESP_ERR_NO_MEM;
    }
    response.buffer[0] = '\0';

    // Configurar cliente HTTP
    esp_http_client_config_t config = {
        .url = full_url,
        .method = HTTP_METHOD_PUT,  // PUT para sobreescribir todo el documento
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
        .event_handler = client_event_handler,
        .user_data = &response,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(response.buffer);
        free(full_url);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, jsonPayload, strlen(jsonPayload));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);
    free(full_url);

    if (err != ESP_OK || (status != 200 && status != 201)) {
        ESP_LOGE(TAG, "Error al sobrescribir documento. Código HTTP: %d", status);
        free(response.buffer);
        return ESP_FAIL;
    }

    if (response.length >= max_len) {
        ESP_LOGW(TAG, "Respuesta demasiado larga (%zu bytes), se truncará a %zu bytes", response.length, max_len - 1);
        strncpy(outResponse, response.buffer, max_len - 1);
        outResponse[max_len - 1] = '\0';
    } else {
        strcpy(outResponse, response.buffer);
    }

    free(response.buffer);
    return ESP_OK;
}



esp_err_t firestore_update_document(const char *subpath, const char *jsonPayload, const char *updateMask, char *outResponse, size_t max_len) {
    if (!firestore_is_initialized()) return ESP_ERR_INVALID_STATE;
    if (!subpath || !jsonPayload || !outResponse || max_len == 0) return ESP_ERR_INVALID_ARG;

    // Construir la URL base con firestore_build_url
    char *url_base = firestore_build_url(subpath);
    if (!url_base) return ESP_ERR_NO_MEM;

    // Preparar la cadena para el query updateMask (si no se pasa, no se añade)
    const char *query_format = "?updateMask.fieldPaths=%s";
    size_t query_len = (updateMask && strlen(updateMask) > 0) ? strlen(query_format) + strlen(updateMask) : 0;
    
    size_t full_url_len = strlen(url_base) + query_len + 1; // +1 para '\0'
    char *full_url = malloc(full_url_len);
    if (!full_url) {
        free(url_base);
        return ESP_ERR_NO_MEM;
    }

    if (query_len > 0) {
        snprintf(full_url, full_url_len, "%s?updateMask.fieldPaths=%s", url_base, updateMask);
    } else {
        // No updateMask, solo copiar URL base
        strncpy(full_url, url_base, full_url_len);
        full_url[full_url_len - 1] = '\0';
    }
    free(url_base);

    // Preparar buffer para respuesta
    http_response_t response = {
        .buffer = malloc(2048),
        .length = 0,
        .capacity = 2048
    };
    if (!response.buffer) {
        free(full_url);
        return ESP_ERR_NO_MEM;
    }
    response.buffer[0] = '\0';

    esp_http_client_config_t config = {
        .url = full_url,
        .method = HTTP_METHOD_PATCH,  // PATCH para actualizar parcialmente
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
        .event_handler = client_event_handler,
        .user_data = &response,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(response.buffer);
        free(full_url);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, jsonPayload, strlen(jsonPayload));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);
    free(full_url);

    if (err != ESP_OK || (status != 200 && status != 201)) {
        ESP_LOGE(TAG, "Error al actualizar documento. Código HTTP: %d", status);
        free(response.buffer);
        return ESP_FAIL;
    }

    if (response.length >= max_len) {
        strncpy(outResponse, response.buffer, max_len - 1);
        outResponse[max_len - 1] = '\0';
    } else {
        strcpy(outResponse, response.buffer);
    }

    free(response.buffer);
    return ESP_OK;
}



