#include "dispenser.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "cJSON.h"  // Librería para parsear (deserializar) y generar (serializar) datos en formato JSON, útil para interpretar comandos o enviar información estructurada (por ejemplo, en mensajes MQTT).
#include "rtc_time.h"
#include "esp_timer.h"
#include <freertos/FreeRTOS.h> 
#include "freertos/semphr.h"
#include "nvs_utils.h"
#include "firestore.h"
#include <freertos/semphr.h>


#define TAG "dispenser"
#define ISO_DATE_STR_LEN 25 // Tamaño suficiente para: 2025-08-02T19:00:00Z + terminador

const char FIRESTORE_PROJECT_ID[] = "reptitrack-946e0";

#define FULL_DOC_NAME_LEN 512


//static bool sensor_estado_anterior = true; // Asumimos HIGH al inicio (sensor no detecta)

static TaskHandle_t dispenser_task_handle = NULL;
static TaskHandle_t fetch_task_handle = NULL;  // Declara globalmente, igual que dispenser_task_handle

static bool ronda_finalizada = false;


static DispenserCompartments g_compartimientos_global;
static SemaphoreHandle_t g_compartimientos_mutex = NULL;

static SemaphoreHandle_t g_fetch_pause_semaphore = NULL;




#define TERRARIO_ID_MAX_LEN 64



bool get_terrario_id_from_nvs(char *terrario_id_buf, size_t buf_len) {
    if (!terrario_id_buf || buf_len == 0) return false;

    memset(terrario_id_buf, 0, buf_len);
    size_t actual_len = 0;

    esp_err_t err = nvs_utils_load_blob("terrarios", "id", terrario_id_buf, buf_len, &actual_len);
    if (err != ESP_OK || actual_len == 0 || actual_len > buf_len) {
        ESP_LOGE(TAG, "Error cargando terrario_id desde NVS: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}


void imprimir_compartimientos_global(void) {
    if (xSemaphoreTake(g_compartimientos_mutex, pdMS_TO_TICKS(100))) {
        ESP_LOGI(TAG, "Imprimiendo compartimientos globales. Total: %d", g_compartimientos_global.count);

        for (int i = 0; i < g_compartimientos_global.count; i++) {
            DispenserCompartment* comp = &g_compartimientos_global.compartments[i];

            char fecha_str[32];
            strftime(fecha_str, sizeof(fecha_str), "%Y-%m-%d %H:%M:%S", &comp->fecha_programada);

            ESP_LOGI(TAG, "Compartimiento %d:", comp->id);
            ESP_LOGI(TAG, "  Fecha programada: %s", fecha_str);
            ESP_LOGI(TAG, "  Dispensado: %s", comp->dispensado ? "Si" : "No");
            ESP_LOGI(TAG, "  Manual: %s", comp->manual ? "Si" : "No");
        }

        xSemaphoreGive(g_compartimientos_mutex);
    } else {
        ESP_LOGW(TAG, "No se pudo tomar mutex para imprimir compartimientos globales");
    }
}

bool firestore_fetch_update_dispensed(DispenserActuator* self, int compartment_id) {
    if (!self || compartment_id < 0) {
        ESP_LOGE(TAG, "Parámetros inválidos: self=%p, compartment_id=%d", self, compartment_id);
        return false;
    }
    
    ESP_LOGI(TAG, "Iniciando actualizacion de dispensado para compartimiento_id=%d", compartment_id);

    // Cargar datos de Firestore desde NVS
    firestore_t data = {0};
    esp_err_t err = firestore_load(&data);
    if (err != ESP_OK || strlen(data.user_id) == 0) {
        return false;
    }

    // Obtener terrario_id desde NVS
    char terrario_id[64] = {0};
    if (!get_terrario_id_from_nvs(terrario_id, sizeof(terrario_id))) {
        return false;
    }

    // Construir path del documento
    char get_subpath[256] = {0};
    snprintf(get_subpath, sizeof(get_subpath),
             "terrarios/%s/dispositivos/esp01/actuadores/dispensador",
             terrario_id);

    // Obtener documento de Firestore
    char *document_json = NULL;
    size_t json_len = 0;
    err = firestore_get_document(get_subpath, &document_json, &json_len);
    if (err != ESP_OK || !document_json) {
        if (document_json) free(document_json);
        return false;
    }

    // Parsear JSON
    cJSON *root = cJSON_Parse(document_json);
    free(document_json);
    if (!root) {
        return false;
    }

    // Navegar estructura JSON
    cJSON *fields = cJSON_GetObjectItem(root, "fields");
    cJSON *programaciones = fields ? cJSON_GetObjectItem(fields, "programaciones") : NULL;
    cJSON *arrayValue = programaciones ? cJSON_GetObjectItem(programaciones, "arrayValue") : NULL;
    cJSON *values = arrayValue ? cJSON_GetObjectItem(arrayValue, "values") : NULL;

    if (!values || !cJSON_IsArray(values)) {
        cJSON_Delete(root);
        return false;
    }

    // Buscar compartimiento específico
    bool found = false;
    int arr_size = cJSON_GetArraySize(values);

    for (int i = 0; i < arr_size; i++) {
        cJSON *comp_item = cJSON_GetArrayItem(values, i);
        if (!comp_item) continue;

        cJSON *mapValue = cJSON_GetObjectItem(comp_item, "mapValue");
        cJSON *comp_fields = mapValue ? cJSON_GetObjectItem(mapValue, "fields") : NULL;
        if (!comp_fields) continue;

        // Verificar ID del compartimiento
        cJSON *id_obj = cJSON_GetObjectItem(comp_fields, "id");
        if (!id_obj) continue;
        cJSON *id_value = cJSON_GetObjectItem(id_obj, "integerValue");
        if (!id_value || !cJSON_IsString(id_value)) continue;

        int current_id = atoi(id_value->valuestring);
        
        if (current_id == compartment_id) {
            
            // Actualizar campo 'dispensado'
            cJSON *dispensado_obj = cJSON_GetObjectItem(comp_fields, "dispensado");
            if (!dispensado_obj) {
                dispensado_obj = cJSON_CreateObject();
                cJSON_AddItemToObject(comp_fields, "dispensado", dispensado_obj);
            }
            
            cJSON *bool_value = cJSON_GetObjectItem(dispensado_obj, "booleanValue");
            if (!bool_value) {
                cJSON_AddBoolToObject(dispensado_obj, "booleanValue", true);
            } else {
                cJSON_SetBoolValue(bool_value, true);
            }
            
            found = true;
            break;
        }
    }

    if (!found) {
        cJSON_Delete(root);
        return false;
    }

    // Preparar JSON para PATCH - Usamos el root completo ya modificado
    char *modified_json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!modified_json_str) {
        return false;
    }

  // Construir path para PATCH terrario
    char patch_subpath[256] = {0};
    snprintf(patch_subpath, sizeof(patch_subpath),
             "terrarios/%s/dispositivos/esp01/actuadores/dispensador",
          		terrario_id);

    // Campo(s) a modificar (updateMask)
    const char *updateMask = "programaciones";

    // Buffer para respuesta
    char respuesta[2048];
    

    // Llamar a la función actualizada con updateMask
    err = firestore_update_document(patch_subpath, modified_json_str, updateMask, respuesta, sizeof(respuesta));
    free(modified_json_str);

	if (err == ESP_OK) {
	} else {
	}
	    
    return true;
}


// Funciones internas para control motor (ejemplo)
static void dispenser_motor_start(void* self_ptr) {
    DispenserActuator* self = (DispenserActuator*)self_ptr;

    gpio_set_level(self->pinIN3, 1);
    gpio_set_level(self->pinIN4, 0);

    ledc_set_duty(DISPENSER_PWM_MODO, DISPENSER_PWM_CANAL, DISPENSER_PWM_DUTY);
    ledc_update_duty(DISPENSER_PWM_MODO, DISPENSER_PWM_CANAL);

    self->enMovimiento = true;
    self-> aspaDetectada = false;
    
    ESP_LOGI(TAG, "Motor iniciado");
}

static void dispenser_motor_stop(void* self_ptr) {
    DispenserActuator* self = (DispenserActuator*)self_ptr;

    ESP_LOGI(TAG, "Deteniendo motor suavemente...");

    // Frenado progresivo: decrementa el PWM en pasos para detener el motor suavemente
    for (int duty = DISPENSER_PWM_DUTY; duty > 0; duty -= 5) {
        ledc_set_duty(DISPENSER_PWM_MODO, DISPENSER_PWM_CANAL, duty);
        ledc_update_duty(DISPENSER_PWM_MODO, DISPENSER_PWM_CANAL);
        vTaskDelay(pdMS_TO_TICKS(10)); // Delay de 10 ms para suavizar frenado
    }

    // Apagar completamente pines y PWM
    gpio_set_level(self->pinIN3, 0);
    gpio_set_level(self->pinIN4, 0);
    ledc_set_duty(DISPENSER_PWM_MODO, DISPENSER_PWM_CANAL, 0);
    ledc_update_duty(DISPENSER_PWM_MODO, DISPENSER_PWM_CANAL);

    // Espera final para asegurar detención completa
    vTaskDelay(pdMS_TO_TICKS(DISPENSER_TIEMPO_FRENADO_MS));

   if (self->compartimientoActual >= 0 && self->compartimientoActual < self->compartimientos.count) {
                // Marcar como dispensado localmente
                self->compartimientos.compartments[self->compartimientoActual].dispensado = true;
                ESP_LOGI(TAG, "Compartimiento %d marcado como dispensado.", 
                         self->compartimientos.compartments[self->compartimientoActual].id);

                if (xSemaphoreTake(g_compartimientos_mutex, pdMS_TO_TICKS(100))) {
                    if (self->compartimientoActual < g_compartimientos_global.count) {
                        g_compartimientos_global.compartments[self->compartimientoActual].dispensado = true;

                        int compart_id_real = g_compartimientos_global.compartments[self->compartimientoActual].id;
                        xSemaphoreGive(g_compartimientos_mutex);

                        if (compart_id_real >= 0) {
                            bool sem_taken = false;
                            if (g_fetch_pause_semaphore != NULL) {
                                if (xSemaphoreTake(g_fetch_pause_semaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
                                    ESP_LOGI(TAG, "fetch_task bloqueada para actualizacion Firestore");
                                    sem_taken = true;
                                } else {
                                    ESP_LOGW(TAG, "No pudo bloquear fetch_task antes de actualizar Firestore");
                                }
                            }

                            bool resultado = firestore_fetch_update_dispensed(self, compart_id_real);
                            if (!resultado) {
                                ESP_LOGE(TAG, "Error actualizando Firestore para compartimiento ID %d", compart_id_real);
                            }
                            
                            if (sem_taken) {
                                xSemaphoreGive(g_fetch_pause_semaphore);
                                ESP_LOGI(TAG, "fetch_task reactivada despues de actualización Firestore");
                            }
                        } else {
                            ESP_LOGE(TAG, "ID de compartimiento inválido para actualizar Firestore");
                        }
                    } else {
                        xSemaphoreGive(g_compartimientos_mutex);
                        ESP_LOGE(TAG, "Índice compartimientoActual fuera de rango");
                    }
                } else {
                    ESP_LOGW(TAG, "No se pudo tomar mutex para actualizar compartimientos globales");
                }
            }
    
   
    bool todos_dispensados = true;
	for (int i = 0; i < self->compartimientos.count; i++) {
	    if (!self->compartimientos.compartments[i].dispensado) {
	        todos_dispensados = false;
	        break;
	    }
	}
	
	
if (todos_dispensados) {
    ronda_finalizada = true;
    ESP_LOGI(TAG, "Ronda finalizada: todos los compartimientos dispensados.");
}

    self->enMovimiento = false; 
    
    ESP_LOGI(TAG, "Motor detenido.");
}


void dispenser_check_timeout(DispenserActuator* dispenser) {
    if (!dispenser || !dispenser->enMovimiento) return;

    // Tiempo actual en milisegundos
    uint64_t now_ms = esp_timer_get_time() / 1000;

    // Verifica si excede el tiempo máximo de giro permitido
    if ((now_ms - dispenser->tiempoInicioMovimiento) > DISPENSER_TIEMPO_MAX_MOVIMIENTO_MS) {
        ESP_LOGW(TAG, "Tiempo maximo de giro excedido! Deteniendo motor por seguridad.");

        if (dispenser->stop_motor) {
            dispenser->stop_motor(dispenser);
        } else {
            ESP_LOGE(TAG, "No se pudo detener el motor: stop_motor no definido");
        }

        // Reinicia estado
        dispenser->aspaDetectada = false;
        dispenser->compartimientoActual = -1;
    }
}


// Funciones tipo Actuator (wrappers que reciben Actuator* y hacen casteo a DispenserActuator*)

bool dispenser_actuator_init(Actuator* actuator) {
	
	    g_compartimientos_mutex = xSemaphoreCreateMutex();
    if (g_compartimientos_mutex == NULL) {
        ESP_LOGE(TAG, "No se pudo crear el mutex g_compartimientos_mutex");
        return false;
    }
    memset(&g_compartimientos_global, 0, sizeof(g_compartimientos_global));
	
	 g_fetch_pause_semaphore = xSemaphoreCreateBinary();
    if (g_fetch_pause_semaphore == NULL) {
        ESP_LOGE(TAG, "No pudo crearse semáforo g_fetch_pause_semaphore");
        return false;
    }
    // Al principio, el semáforo está "liberado", esto significa:
    // Lo damos para indicar que la tarea fetch puede correr.
    xSemaphoreGive(g_fetch_pause_semaphore);
	
	
    DispenserActuator* self = (DispenserActuator*) actuator;

    // Configuración GPIO salida para motor
    gpio_config_t io_out_conf = {
        .pin_bit_mask = ((1ULL << self->pinIN3) | (1ULL << self->pinIN4)),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_out_conf);

    // Configuración GPIO entrada para sensor FC51
    gpio_config_t io_in_conf = {
        .pin_bit_mask = (1ULL << self->pinSensorFC51),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // Ajusta según necesidad
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_in_conf);

    // Configuración PWM
		ledc_timer_config_t timer_conf = {
		    .speed_mode = DISPENSER_PWM_MODO,
		    .timer_num = DISPENSER_PWM_TIMER_ID,
		    .duty_resolution = DISPENSER_PWM_RESOLUCION,
		    .freq_hz = DISPENSER_PWM_FRECUENCIA,
		    .clk_cfg = LEDC_AUTO_CLK
		};
		ledc_timer_config(&timer_conf);
		
		ledc_channel_config_t ledc_conf = {
		    .gpio_num = self->pinENB,
		    .speed_mode = DISPENSER_PWM_MODO,
		    .channel = DISPENSER_PWM_CANAL,
		    .timer_sel = DISPENSER_PWM_TIMER_ID,
		    .duty = 0,
		    .hpoint = 0
		};
		ledc_channel_config(&ledc_conf);


    self->enMovimiento = false;
    self->compartimientoActual = -1;

    ESP_LOGI(TAG, "Dispenser initialized");
    return true;
}




void dispenser_actuator_start(Actuator* actuator) {
    DispenserActuator* self = (DispenserActuator*) actuator;

    // Si la tarea del motor ya está en ejecución, no crear nada más
    if (dispenser_task_handle != NULL) {
        ESP_LOGW(TAG, "tarea dispenser_task ya en ejecucion");
        return;
    }

    // Crear dispenser_task (control motor + sensor)
    BaseType_t result = xTaskCreate(
        dispenser_task,
        "dispenser_task",
        8192,         // Aumentado a 8KB para evitar stack overflow
        (void*) self,
        5,
        &dispenser_task_handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear la tarea dispenser_task");
        dispenser_task_handle = NULL;
        return;
    }

/**/
    // Crear fetch_task (obtiene datos periódicamente)
    if (fetch_task_handle == NULL) {
        BaseType_t fetch_result = xTaskCreate(
            fetch_task,
            "fetch_task",
            8192,       // Tamaño apropiado para fetch, ajustar si es necesario
            (void*) self,
            5,
            &fetch_task_handle
        );

        if (fetch_result != pdPASS) {
            ESP_LOGE(TAG, "No se pudo crear la tarea fetch_task");
            fetch_task_handle = NULL;
            // Opcional: eliminar dispenser_task para evitar estado inconsistente
        }
    }
}


bool dispenser_parse_compartments_json(const char* json_str, DispenserCompartments* compartimientos) {
    if (!json_str || !compartimientos) return false;

    cJSON* root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Error al parsear JSON");
        return false;
    }

    cJSON* compartments = cJSON_GetObjectItem(root, "compartments");
    if (!cJSON_IsArray(compartments)) {
        ESP_LOGE(TAG, "Campo 'compartments' no es un array");
        cJSON_Delete(root);
        return false;
    }

    int count = cJSON_GetArraySize(compartments);
    if (count > DISPENSER_MAX_COMPARTMENTS) count = DISPENSER_MAX_COMPARTMENTS;


    for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(compartments, i);
        if (!item) continue;

        DispenserCompartment* comp = &compartimientos->compartments[i];

        cJSON* map_value = cJSON_GetObjectItem(item, "mapValue");
        if (!map_value || !cJSON_IsObject(map_value)) {
            ESP_LOGW(TAG, "No se encontró 'mapValue' para compartimiento índice %d", i);
            continue;
        }

        cJSON* fields = cJSON_GetObjectItem(map_value, "fields");
        if (!fields || !cJSON_IsObject(fields)) {
            ESP_LOGW(TAG, "No se encontró 'fields' en 'mapValue' para índice %d", i);
            continue;
        }

        cJSON* id_obj = cJSON_GetObjectItem(fields, "id");
        cJSON* fecha_obj = cJSON_GetObjectItem(fields, "fechaProgramada");
        cJSON* dispensado_obj = cJSON_GetObjectItem(fields, "dispensado");
        cJSON* manual_obj = cJSON_GetObjectItem(fields, "manual");

        if (!id_obj || !fecha_obj || !dispensado_obj || !manual_obj) {
            ESP_LOGW(TAG, "Faltan campos en JSON para compartimiento índice %d", i);
            continue;
        }

        cJSON* id = cJSON_GetObjectItem(id_obj, "integerValue");
        cJSON* timestamp = cJSON_GetObjectItem(fecha_obj, "timestampValue");
        cJSON* dispensado = cJSON_GetObjectItem(dispensado_obj, "booleanValue");
        cJSON* manual = cJSON_GetObjectItem(manual_obj, "booleanValue");

        if (!cJSON_IsString(id) || !cJSON_IsString(timestamp) ||
            (!cJSON_IsBool(dispensado) && dispensado == NULL) ||
            (!cJSON_IsBool(manual) && manual == NULL)) {
            ESP_LOGW(TAG, "Campos inválidos para compartimiento índice %d", i);
            continue;
        }

        comp->id = atoi(id->valuestring);

        // Usar directamente tu función para convertir el string ISO8601 a struct tm
        if (rtc_time_parse_iso8601_to_tm(timestamp->valuestring, &comp->fecha_programada) != ESP_OK) {
            ESP_LOGW(TAG, "Fecha inválida para id %d: %s", comp->id, timestamp->valuestring);
            memset(&comp->fecha_programada, 0, sizeof(struct tm));
        }

        comp->dispensado = cJSON_IsTrue(dispensado);
        comp->manual = cJSON_IsTrue(manual);
    }

    compartimientos->count = count;
    cJSON_Delete(root);
    
    return true;
}



bool dispenser_actuator_fetch(Actuator* actuator) {
    (void) actuator;

    if (!firestore_is_initialized()) {
        ESP_LOGE(TAG, "Firestore no inicializado");
        return false;
    }

    // Leer terrario_id guardado en NVS
    char terrario_id[TERRARIO_ID_MAX_LEN];
    if (!get_terrario_id_from_nvs(terrario_id, sizeof(terrario_id))) {
        ESP_LOGE(TAG, "No se pudo obtener terrario_id de NVS");
        return false;
    }

    // Construir subpath relativo para Firestore usando terrario_id
    // Ejemplo: "terrarios/{terrario_id}/dispositivos/esp01/actuadores/dispensador"
    char subpath[128];
    snprintf(subpath, sizeof(subpath), "terrarios/%s/dispositivos/esp01/actuadores/dispensador", terrario_id);

    char *full_url = firestore_build_url(subpath);
    if (!full_url) {
        ESP_LOGE(TAG, "No se pudo construir URL para Firestore");
        return false;
    }

    // Usar firestore_get_document para obtener JSON
    char *response_json = NULL;
    size_t response_len = 0;
    esp_err_t err = firestore_get_document(subpath, &response_json, &response_len);
    free(full_url); // full_url no se usa directamente aquí porque firestore_get_document la construye internamente

    if (err != ESP_OK || !response_json) {
        ESP_LOGE(TAG, "Error al obtener documento Firestore");
        if (response_json) free(response_json);
        return false;
    }

    // Parsear JSON Firestore para extraer el array "programaciones"
    cJSON* root = cJSON_Parse(response_json);
    free(response_json);
    if (!root) {
        ESP_LOGE(TAG, "JSON Firestore inválido");
        return false;
    }

    cJSON* fields = cJSON_GetObjectItem(root, "fields");
    if (!fields) {
        ESP_LOGE(TAG, "No se encontró 'fields' en JSON");
        cJSON_Delete(root);
        return false;
    }

    cJSON* programaciones = cJSON_GetObjectItem(fields, "programaciones");
    if (!programaciones) {
        ESP_LOGE(TAG, "No se encontró 'programaciones' en 'fields'");
        cJSON_Delete(root);
        return false;
    }

    cJSON* arrayValue = cJSON_GetObjectItem(programaciones, "arrayValue");
    if (!arrayValue) {
        ESP_LOGE(TAG, "No se encontró 'arrayValue' en 'programaciones'");
        cJSON_Delete(root);
        return false;
    }

    cJSON* values = cJSON_GetObjectItem(arrayValue, "values");
    if (!values || !cJSON_IsArray(values)) {
        ESP_LOGE(TAG, "No se encontró arreglo 'values' en 'arrayValue'");
        cJSON_Delete(root);
        return false;
    }

    cJSON* new_root = cJSON_CreateObject();
    cJSON_AddItemToObject(new_root, "compartments", cJSON_Duplicate(values, 1));
    cJSON_Delete(root);

    char* compartments_json_str = cJSON_PrintUnformatted(new_root);
    cJSON_Delete(new_root);

    if (!compartments_json_str) {
        ESP_LOGE(TAG, "No se pudo crear string JSON de compartimientos");
        return false;
    }

    DispenserCompartments new_compartments;
    memset(&new_compartments, 0, sizeof(new_compartments));
    bool success = dispenser_parse_compartments_json(compartments_json_str, &new_compartments);
    free(compartments_json_str);

    if (!success) {
        ESP_LOGE(TAG, "Error parseando compartimientos");
        return false;
    }

    if (xSemaphoreTake(g_compartimientos_mutex, pdMS_TO_TICKS(100))) {
        g_compartimientos_global = new_compartments;
        xSemaphoreGive(g_compartimientos_mutex);
        ESP_LOGI(TAG, "Compartimientos actualizados desde Firestore");
        return true;
    } else {
        ESP_LOGW(TAG, "No se pudo tomar mutex para actualizar compartimientos");
        return false;
    }
}

void check_scheduled_compartments(DispenserActuator* dispenser) {
    if (!dispenser) {
        ESP_LOGW(TAG, "puntero dispenser es NULL, saliendo");
        return;
    }

    if (dispenser->enMovimiento) {
        return;
    }

    time_t now;
    struct tm timeinfo;

    // Obtener la hora actual formateada en struct tm local
    if (rtc_time_get_current(&now) != ESP_OK || !localtime_r(&now, &timeinfo)) {
        ESP_LOGE(TAG, "Error al obtener hora actual");
        return;
    }

    char hora_actual_str[32];
    if (rtc_time_get_formatted_readable(hora_actual_str, sizeof(hora_actual_str)) != ESP_OK) {
        snprintf(hora_actual_str, sizeof(hora_actual_str), "Hora_actual_desconocida");
    }

    for (int i = 0; i < dispenser->compartimientos.count; i++) {
        DispenserCompartment* comp = &dispenser->compartimientos.compartments[i];

        if (comp->dispensado) {
            // Ignorar si ya fue dispensado
            continue;
        }

        bool dispensar = false;

        if (comp->manual) {
            // Si es manual, dispensar siempre
            dispensar = true;
        } else {
            // Si no es manual, dispensar solo si la hora fue alcanzada
            struct tm comp_time = comp->fecha_programada;

            bool tiempo_alcanzado =
                (timeinfo.tm_year > comp_time.tm_year) ||
                (timeinfo.tm_year == comp_time.tm_year && timeinfo.tm_mon > comp_time.tm_mon) ||
                (timeinfo.tm_year == comp_time.tm_year && timeinfo.tm_mon == comp_time.tm_mon && timeinfo.tm_mday > comp_time.tm_mday) ||
                (timeinfo.tm_year == comp_time.tm_year && timeinfo.tm_mon == comp_time.tm_mon && timeinfo.tm_mday == comp_time.tm_mday &&
                 timeinfo.tm_hour > comp_time.tm_hour) ||
                (timeinfo.tm_year == comp_time.tm_year && timeinfo.tm_mon == comp_time.tm_mon && timeinfo.tm_mday == comp_time.tm_mday &&
                 timeinfo.tm_hour == comp_time.tm_hour && timeinfo.tm_min >= comp_time.tm_min);

            if (tiempo_alcanzado) {
                dispensar = true;
            }
        }

        if (dispensar) {
            char hora_programada_str[32];
            strftime(hora_programada_str, sizeof(hora_programada_str), "%Y-%m-%d %H:%M:%S", &comp->fecha_programada);

            ESP_LOGI(TAG,
                     ">> Activando compartimiento %d a las %s (hora actual %s)...",
                     comp->id, hora_programada_str, hora_actual_str);

            dispenser->compartimientoActual = i;
            dispenser->aspaDetectada = false;
            dispenser->tiempoInicioMovimiento = esp_timer_get_time() / 1000;

            if (dispenser->start_motor) {
                dispenser->enMovimiento = true;
                dispenser->start_motor(dispenser);
            } else {
                ESP_LOGE(TAG, "Error: función start_motor no definida");
            }
            break;  // activar solo uno a la vez
        }
    }
}



// Función para detectar el paso del aspa usando sensor FC51
void dispenser_check_aspa_detected(DispenserActuator* self) {
    if (!self->enMovimiento) {
        return;
    }

    bool aspaDetectada = gpio_get_level(self->pinSensorFC51) == 0;

    if (aspaDetectada) {
        ESP_LOGI(TAG, "Aspa detectada. Deteniendo motor.");
        if (self->stop_motor) {
			self->enMovimiento = false; 
            self->stop_motor(self);
        }
        

        self->aspaDetectada = true;
        self->compartimientoActual = -1;
    }
    dispenser_check_timeout(self);
}


bool dispenser_actuator_update(Actuator* actuator) {
     // DispenserActuator* self = (DispenserActuator*) actuator;

    ESP_LOGW(TAG, "update_compartment_status no implementado");
    return false;
}

bool dispenser_actuator_deinit(Actuator* actuator) {
    DispenserActuator* self = (DispenserActuator*) actuator;
    if (self->stop_motor) {
        self->stop_motor(self);
    } else {
        ESP_LOGW(TAG, "No stop_motor definido");
    }
    return true;
}

// Crear el dispenser y devolverlo ya como Actuator* para registrar en el manager
Actuator* dispenser_create(int pinIN3, int pinIN4, int pinENB, int pinSensorFC51) {
    DispenserActuator* dispenser = malloc(sizeof(DispenserActuator));
    if (!dispenser) return NULL;

    memset(dispenser, 0, sizeof(DispenserActuator));
    
    dispenser->pinIN3 = pinIN3;
    dispenser->pinIN4 = pinIN4;
    dispenser->pinENB = pinENB;
    dispenser->pinSensorFC51 = pinSensorFC51;

    dispenser->base.name = "dispenser_01";
    dispenser->base.init = dispenser_actuator_init;
    dispenser->base.start = dispenser_actuator_start;
	dispenser->base.fetch_from_db = dispenser_actuator_fetch;
	dispenser->base.update_to_db = dispenser_actuator_update;
    dispenser->base.deinit = dispenser_actuator_deinit;

    dispenser->start_motor = dispenser_motor_start;
    dispenser->stop_motor = dispenser_motor_stop;


    return (Actuator*)dispenser;
}

void dispenser_destroy(Actuator* actuator) {
    if (!actuator) return;

    DispenserActuator* dispenser = (DispenserActuator*) actuator;

    if (dispenser->base.name) {
        free((void*)dispenser->base.name);
    }
    
    free(dispenser);
}



static void fetch_task(void* pvParameter) {
    DispenserActuator* self = (DispenserActuator*) pvParameter;

    while (1) {
        // Intentamos tomar semáforo de pausa con timeout 0 (no bloqueante)
        // Si no podemos tomarlo, significa que debemos pausarnos (esperar)
        if (xSemaphoreTake(g_fetch_pause_semaphore, 0) == pdTRUE) {
            // El semáforo estaba libre: podemos hacer fetch
            if (!dispenser_actuator_fetch((Actuator*)self)) {
                ESP_LOGW(TAG, "dispenser_actuator_fetch fallo");
            } else {
                ESP_LOGI(TAG, "dispenser_actuator_fetch OK");
            }
            // Liberamos el semáforo para permitir otro ciclo
            xSemaphoreGive(g_fetch_pause_semaphore);
        } else {
            ESP_LOGW(TAG, "fetch_task pausada temporalmente por dispensado");
            // Si está pausada, sólo espera un poco y vuelve a intentar luego
        }
        vTaskDelay(pdMS_TO_TICKS(5000));  // Ciclo cada 5 segundos
    }
}


static void dispenser_task(void* pvParameter) {
    DispenserActuator* self = (DispenserActuator*) pvParameter;

    while (1) {
		        
		    if (xSemaphoreTake(g_compartimientos_mutex, pdMS_TO_TICKS(50))) {
			    self->compartimientos = g_compartimientos_global;
			    xSemaphoreGive(g_compartimientos_mutex);
			}

            check_scheduled_compartments(self);
           
      
        if (self->enMovimiento) {
            dispenser_check_aspa_detected(self);
        }

		  vTaskDelay(pdMS_TO_TICKS(20));
    }
}






