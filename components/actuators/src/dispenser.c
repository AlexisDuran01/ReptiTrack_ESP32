#include "dispenser.h"
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

#define TAG "dispenser"
#define ISO_DATE_STR_LEN 25 // Tamaño suficiente para: 2025-08-02T19:00:00Z + terminador

//static bool sensor_estado_anterior = true; // Asumimos HIGH al inicio (sensor no detecta)

static TaskHandle_t dispenser_task_handle = NULL;
static TaskHandle_t fetch_task_handle = NULL;  // Declara globalmente, igual que dispenser_task_handle

static bool ronda_finalizada = false;

static char *g_json_compartimientos = NULL;

static SemaphoreHandle_t g_json_mutex = NULL;    // Mutex para proteger JSON y estructura compartida

const char* get_json_compartimientos(void) {
    const char* json = NULL;
    if (xSemaphoreTake(g_json_mutex, portMAX_DELAY)) {
        json = g_json_compartimientos;  // Ojo: no lo modifiques desde afuera, solo lectura rápida
        xSemaphoreGive(g_json_mutex);
    }
    return json;
}

bool set_json_compartimientos(const char* nuevo_json) {
    if (!nuevo_json) return false;

    bool resultado = false;
    if (xSemaphoreTake(g_json_mutex, portMAX_DELAY)) {
        if (g_json_compartimientos) {
            free(g_json_compartimientos);
            g_json_compartimientos = NULL;
        }

        g_json_compartimientos = strdup(nuevo_json);
        resultado = (g_json_compartimientos != NULL);

        xSemaphoreGive(g_json_mutex);
    }
    return resultado;
}


static void time_to_iso8601_utc(time_t t, char* out_str, size_t len) {
    struct tm tm_utc;
    gmtime_r(&t, &tm_utc);
    strftime(out_str, len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}


// Función para crear el JSON dinámico con fechas basadas en la hora actual
char* crear_json_compartimientos_dinamico(int num_compartimientos) {
    if (num_compartimientos > DISPENSER_MAX_COMPARTMENTS) {
        num_compartimientos = DISPENSER_MAX_COMPARTMENTS;
    }

    time_t now;
    if (rtc_time_get_current(&now) != ESP_OK) {
        ESP_LOGE("JSON", "No se pudo obtener hora actual");
        return NULL;
    }

    // Tamaño estimado para el JSON (ajustar según necesidad)
    // Aquí se calcula un tamaño suficiente aproximado para evitar muchas realocaciones
    size_t buffer_size = 1024;
    char* json_str = malloc(buffer_size);
    if (!json_str) {
        ESP_LOGE("JSON", "No hay memoria para el JSON");
        return NULL;
    }
    json_str[0] = '\0';

    strcat(json_str, "{ \"compartments\": [");

	for (int i = 0; i < num_compartimientos; i++) {
	    char iso_date[ISO_DATE_STR_LEN];
	
	time_t compart_time = now + (i + 1) * 60;  // Primer compartimiento a +1 min, luego +1 min adicionales
	
	    time_to_iso8601_utc(compart_time, iso_date, sizeof(iso_date));
	
	    // Construir json del compartimiento temporal
	    char compartment_json[256];
	    snprintf(compartment_json, sizeof(compartment_json),
	             "{\"id\": \"A%d\", \"fecha_programada\": \"%s\", \"dispensado\": false, \"manual\": false}%s",
	             i+1, iso_date, (i < num_compartimientos - 1) ? "," : "");
	
	    strcat(json_str, compartment_json);
	
	    // Imprimir fecha legible para debug
	    char readable_date[64];
	    struct tm tm_info;
	    gmtime_r(&compart_time, &tm_info);
	    strftime(readable_date, sizeof(readable_date), "%Y-%m-%d %H:%M:%S UTC", &tm_info);
	
	    ESP_LOGI("JSON", "  Compartimiento A%d => Fecha programada: %s", i + 1, readable_date);
	}



    strcat(json_str, "]}");
   
    return json_str;
}

bool actualizar_json_desde_estructura(DispenserActuator* self) {
    if (!self) return false;

    size_t buffer_size = 1024;
    char* json_str = malloc(buffer_size);
    if (!json_str) {
        ESP_LOGE(TAG, "No hay memoria para construir JSON");
        return false;
    }
    json_str[0] = '\0';

    strcat(json_str, "{ \"compartments\": [");

    for (int i = 0; i < self->compartimientos.count; i++) {
        char iso_date[ISO_DATE_STR_LEN];
        time_to_iso8601_utc(self->compartimientos.compartments[i].fecha_programada, iso_date, sizeof(iso_date));

        char comp_json[256];
        snprintf(comp_json, sizeof(comp_json),
            "{\"id\": \"%s\", \"fecha_programada\": \"%s\", \"dispensado\": %s, \"manual\": %s}%s",
            self->compartimientos.compartments[i].id,
            iso_date,
            self->compartimientos.compartments[i].dispensado ? "true" : "false",
            self->compartimientos.compartments[i].manual ? "true" : "false",
            (i < self->compartimientos.count - 1) ? "," : "");

        strcat(json_str, comp_json);
    }

    strcat(json_str, "]}");

    // Reemplazar JSON global protegido por mutex
    bool resultado = false;
    if (xSemaphoreTake(g_json_mutex, portMAX_DELAY) == pdTRUE) {
        if (g_json_compartimientos) {
            free(g_json_compartimientos);
            g_json_compartimientos = NULL;
        }
        g_json_compartimientos = strdup(json_str);
        resultado = (g_json_compartimientos != NULL);
        xSemaphoreGive(g_json_mutex);
    }

    free(json_str);
    return resultado;
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

    // Bloqueo crítico: modificar estado compartido protegido por mutex
    if (xSemaphoreTake(g_json_mutex, portMAX_DELAY) == pdTRUE) {
        if (self->compartimientoActual >= 0 && self->compartimientoActual < self->compartimientos.count) {
            self->compartimientos.compartments[self->compartimientoActual].dispensado = true;
            ESP_LOGI(TAG, "Compartimiento %s dispensado.", self->compartimientos.compartments[self->compartimientoActual].id);
        }

        self->enMovimiento = false;
        self->compartimientoActual = -1;

        xSemaphoreGive(g_json_mutex);
    }

      // Actualiza JSON global desde estructura
    if (!actualizar_json_desde_estructura(self)) {
        ESP_LOGE(TAG, "Error al actualizar JSON tras dispensar motor");
    } else {
        // Imprimir JSON actualizado
        const char* json = get_json_compartimientos();
        if (json) {
        } else {
            ESP_LOGW(TAG, "JSON actualizado no disponible");
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
	
	    g_json_mutex = xSemaphoreCreateMutex();
    if (g_json_mutex == NULL) {
        ESP_LOGE(TAG, "No se pudo crear el mutex g_json_mutex");
        return false;
    }
	
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


// Función para parsear el JSON simulado y rellenar la estructura DispenserCompartments
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

        cJSON* id = cJSON_GetObjectItem(item, "id");
        cJSON* fecha = cJSON_GetObjectItem(item, "fecha_programada");
        cJSON* dispensado = cJSON_GetObjectItem(item, "dispensado");
        cJSON* manual = cJSON_GetObjectItem(item, "manual");

        if (cJSON_IsString(id) && cJSON_IsString(fecha) && cJSON_IsBool(dispensado) && cJSON_IsBool(manual)) {
            strncpy(comp->id, id->valuestring, sizeof(comp->id));
            comp->id[sizeof(comp->id) - 1] = '\0';

            if (rtc_time_parse_iso8601_string(fecha->valuestring, &comp->fecha_programada) != ESP_OK) {
                ESP_LOGW(TAG, "Fecha inválida para %s: %s", comp->id, fecha->valuestring);
                comp->fecha_programada = 0;
            }

            comp->dispensado = cJSON_IsTrue(dispensado);
            comp->manual = cJSON_IsTrue(manual);
        } else {
            ESP_LOGW(TAG, "Datos inválidos en el JSON para compartimiento %d", i);
            // Si quieres que sea estricto, puedes hacer: cJSON_Delete(root); return false;
        }
    }

    compartimientos->count = count;

    cJSON_Delete(root);
    return true;
}


bool dispenser_actuator_fetch(Actuator* actuator) {
    if (!actuator) return false;
    DispenserActuator* dispenser = (DispenserActuator*) actuator;

    // Genera el JSON dinámico
    char* json_dinamico = crear_json_compartimientos_dinamico(5);
    if (!json_dinamico) {
        ESP_LOGE("dispenser_fetch", "No se pudo generar JSON dinámico");
        return false;
    }

    // Guarda el JSON generado en la variable global
    bool ok = set_json_compartimientos(json_dinamico);

    // Parsear desde la variable global
    if (ok) {
        ok = dispenser_parse_compartments_json(g_json_compartimientos, &dispenser->compartimientos);
    }

    free(json_dinamico); // Liberar esta copia local, porque ya hicimos strdup en set_json_compartimientos

    return ok;
}




void check_scheduled_compartments(DispenserActuator* dispenser) {
    if (!dispenser || dispenser->enMovimiento) return;

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

        if (comp->dispensado || comp->manual) continue;

        struct tm comp_time;
        if (!localtime_r(&comp->fecha_programada, &comp_time)) {
            ESP_LOGW(TAG, "Error al convertir fecha programada para %s", comp->id);
            continue;
        }

        // Obtener hora programada legible para el compartimiento
        char hora_programada_str[32];
        if (rtc_time_get_formatted_readable(hora_programada_str, sizeof(hora_programada_str)) != ESP_OK) {
            // Si falla, formatear manualmente usando strftime
            strftime(hora_programada_str, sizeof(hora_programada_str), "%Y-%m-%d %H:%M:%S", &comp_time);
        }

		bool tiempo_alcanzado = (timeinfo.tm_year > comp_time.tm_year) ||
		    (timeinfo.tm_year == comp_time.tm_year && timeinfo.tm_mon > comp_time.tm_mon) ||
		    (timeinfo.tm_year == comp_time.tm_year && timeinfo.tm_mon == comp_time.tm_mon && timeinfo.tm_mday > comp_time.tm_mday) ||
		    (timeinfo.tm_year == comp_time.tm_year && timeinfo.tm_mon == comp_time.tm_mon && timeinfo.tm_mday == comp_time.tm_mday &&
		     timeinfo.tm_hour > comp_time.tm_hour) ||
		    (timeinfo.tm_year == comp_time.tm_year && timeinfo.tm_mon == comp_time.tm_mon && timeinfo.tm_mday == comp_time.tm_mday &&
		     timeinfo.tm_hour == comp_time.tm_hour && timeinfo.tm_min >= comp_time.tm_min);
		     
		if (tiempo_alcanzado){

            ESP_LOGI(TAG, ">> Activando compartimiento %s a las %s (hora actual %s)...",
                     comp->id, hora_programada_str, hora_actual_str);

            dispenser->compartimientoActual = i;
            dispenser->aspaDetectada = false;
            dispenser->tiempoInicioMovimiento = esp_timer_get_time() / 1000;

            if (dispenser->start_motor) {
                dispenser->start_motor(dispenser);
            } else {
                ESP_LOGE(TAG, "Error: función start_motor no definida");
            }
            break;  // activa solo uno
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
        if (xSemaphoreTake(g_json_mutex, portMAX_DELAY) == pdTRUE) {

            if (ronda_finalizada || !g_json_compartimientos) {
                // Genera JSON nuevo solo si la ronda terminó o no hay JSON previo
                char* json_dinamico = crear_json_compartimientos_dinamico(DISPENSER_MAX_COMPARTMENTS);
                if (json_dinamico) {
                    if (g_json_compartimientos) free(g_json_compartimientos);
                    g_json_compartimientos = json_dinamico;

                    if (!dispenser_parse_compartments_json(g_json_compartimientos, &self->compartimientos)) {
                        ESP_LOGE(TAG, "Error al parsear JSON dentro de fetch_task");
                    }

                    ronda_finalizada = false;
                }
            }
            // Opcional: podrías regenerar JSON desde estructura actual si quieres actualizar solo el JSON global
            // sin tocar la estructura, para reflejar actualizaciones hechas internamente.

            xSemaphoreGive(g_json_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}


static void dispenser_task(void* pvParameter) {
    DispenserActuator* self = (DispenserActuator*) pvParameter;

    while (1) {

        if (xSemaphoreTake(g_json_mutex, portMAX_DELAY) == pdTRUE) {
            // Con datos protegidos puedes llamar aquí las funciones que usan la estructura
            check_scheduled_compartments(self);
            xSemaphoreGive(g_json_mutex);
        }

        if (self->enMovimiento) {
            dispenser_check_aspa_detected(self);
        }

		  vTaskDelay(pdMS_TO_TICKS(20));
    }
}


