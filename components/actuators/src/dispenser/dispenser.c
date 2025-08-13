#include "dispenser/dispenser.h"
#include "dispenser/dispenser_motor.h"
#include "dispenser/dispenser_firestore.h"
#include "dispenser/dispenser_utils.h"
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "rtc_time.h"
#include "esp_timer.h"
#include "dispenser/dispenser_led.h"


#define TAG "dispenser"

static TaskHandle_t dispenser_task_handle = NULL;
static TaskHandle_t fetch_task_handle = NULL;

static int ultimo_segundo_mostrado[DISPENSER_MAX_COMPARTMENTS] = {0};

bool ronda_finalizada = false;

// Variables globales para compartimientos y sincronización
DispenserCompartments g_compartimientos_global;
SemaphoreHandle_t g_compartimientos_mutex = NULL;
SemaphoreHandle_t g_fetch_pause_semaphore = NULL;


void check_scheduled_compartments(DispenserActuator* dispenser) {
    if (!dispenser) {
        ESP_LOGW(TAG, "puntero dispenser es NULL, saliendo");
        return;
    }

    if (dispenser->enMovimiento) {
        return;
    }

    if (ronda_finalizada) {
        return;
    }

    time_t now;
    if (rtc_time_get_current(&now) != ESP_OK) {
        ESP_LOGE(TAG, "Error al obtener hora actual");
        return;
    }

    for (int i = 0; i < dispenser->compartimientos.count; i++) {
        DispenserCompartment* comp = &dispenser->compartimientos.compartments[i];
        

        if (comp->dispensado) {
            continue; // Ignorar dispensados
        }

        bool dispensar = false;

         if (comp->manual) {
            dispensar = true;
            ESP_LOGI(TAG, "[Compartimiento ID:%d] Dispensar manual activado", comp->id);
        } else {
			
			double segundos_restantes = difftime(comp->fecha_programada, now);
			int segundos_int = (int)segundos_restantes;
			
			if (segundos_int > 0 && segundos_int <= 5) {
			    if (ultimo_segundo_mostrado[i] != segundos_int) {
			        ESP_LOGI(TAG, "[Compartimiento ID:%d] Faltan %d segundos para dispensar (programado para %lld)", 
			                 comp->id, segundos_int, (long long)comp->fecha_programada);
			        ultimo_segundo_mostrado[i] = segundos_int;
			    }
			} else {
			    ultimo_segundo_mostrado[i] = 0; 
			}
					
			
			if (now >= comp->fecha_programada) {
				    dispensar = true;
				}
            
        }

        if (dispensar) {
            // Mostrar hora actual, programada y cuánto faltaba
            struct tm tm_now, tm_prog;
            char hora_actual_str[32], hora_programada_str[32];
            gmtime_r(&now, &tm_now);
            gmtime_r(&comp->fecha_programada, &tm_prog);
            strftime(hora_actual_str, sizeof(hora_actual_str), "%Y-%m-%d %H:%M:%S", &tm_now);
            strftime(hora_programada_str, sizeof(hora_programada_str), "%Y-%m-%d %H:%M:%S", &tm_prog);

            double segundos_restantes = difftime(comp->fecha_programada, now);
            ESP_LOGI(TAG, "\n\n");	
            ESP_LOGI(TAG, "[Compartimiento ID:%d] Dispensar | Hora actual: %s | Hora programada: %s | Faltaban: %.0f segundos",
                     comp->id, hora_actual_str, hora_programada_str, -segundos_restantes);

            // Imprime el estado de todos los compartimientos
			imprimir_compartimientos_global();
			rtc_time_print_current();	
			
			ESP_LOGI(TAG, "\n\n");	

			
            dispenser->compartimientoActual = i;
            dispenser->aspaDetectada = false;
            dispenser->tiempoInicioMovimiento = esp_timer_get_time() / 1000;

            if (dispenser->start_motor) {
                dispenser->enMovimiento = true;
                dispenser->start_motor(dispenser);
                ESP_LOGI(TAG, "Motor iniciado para compartimiento %d", comp->id);
            } else {
                ESP_LOGE(TAG, "start_motor no definido");
            }
            break;  // Activar solo uno a la vez
        }
    }
}

// --- Tareas FreeRTOS ---

static void fetch_task(void* pvParameter) {
    DispenserActuator* self = (DispenserActuator*) pvParameter;
    while (1) {
        if (xSemaphoreTake(g_fetch_pause_semaphore, 0) == pdTRUE) {
            if (!dispenser_actuator_fetch((Actuator*)self)) {
                ESP_LOGW(TAG, "dispenser_actuator_fetch fallo");
            }
            xSemaphoreGive(g_fetch_pause_semaphore);
        }
        vTaskDelay(pdMS_TO_TICKS(2100));
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
            dispenser_check_timeout(self); // <-- Añade aquí la verificación de timeout
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// --- Funciones tipo Actuator ---

bool dispenser_actuator_init(Actuator* actuator) {
	    dispenser_led_init();
	
	// En tu inicialización, después de checar rtc_time_is_synchronized
	bool synced = false;
	if (rtc_time_is_synchronized(&synced) != ESP_OK || !synced) {
	    ESP_LOGI(TAG, "RTC sincronizando...");
	    rtc_time_sync_with_timezone("America/Mexico_City");
	    dispenser_led_set_synced(false);
	} else {
	    time_t now;
	    if (rtc_time_get_current(&now) != ESP_OK) {
	        ESP_LOGI(TAG, "No se pudo obtener hora, sincronizando...");
	        rtc_time_sync_with_timezone("America/Mexico_City");
	        dispenser_led_set_synced(false);
	    } else {
	        struct tm tm_now;
	        gmtime_r(&now, &tm_now);
	        if (tm_now.tm_year + 1900 < 2025) { // <-- Cambia aquí el año mínimo válido
	            ESP_LOGI(TAG, "Hora RTC inválida (%d), sincronizando...", tm_now.tm_year + 1900);
	            rtc_time_sync_with_timezone("America/Mexico_City");
	          	dispenser_led_set_synced(false);
	        } else {
	            ESP_LOGI(TAG, "RTC ya sincronizado y hora valida");
	           dispenser_led_set_synced(true); // LED encendido fijo si la hora es valida
	           // Si no se llegara aqui solamente parpadearia al hacer la peticion 
	           // y no se mantendria encendido el led
	           
	        }
	    }
	}
	
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
        .pull_up_en = GPIO_PULLUP_ENABLE,
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
    if (dispenser_task_handle != NULL) {
        ESP_LOGW(TAG, "tarea dispenser_task ya en ejecucion");
        return;
    }
    BaseType_t result = xTaskCreate(
        dispenser_task,
        "dispenser_task",
        8192,
        (void*) self,
        5,
        &dispenser_task_handle
    );
    if (result != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear la tarea dispenser_task");
        dispenser_task_handle = NULL;
        return;
    }
    if (fetch_task_handle == NULL) {
        BaseType_t fetch_result = xTaskCreate(
            fetch_task,
            "fetch_task",
            8192,
            (void*) self,
            5,
            &fetch_task_handle
        );
        if (fetch_result != pdPASS) {
            ESP_LOGE(TAG, "No se pudo crear la tarea fetch_task");
            fetch_task_handle = NULL;
        }
    }
}

bool dispenser_actuator_update(Actuator* actuator) {
    ESP_LOGW(TAG, "update_compartment_status no implementado");
    return false;
}

bool dispenser_actuator_deinit(Actuator* actuator) {
    DispenserActuator* self = (DispenserActuator*) actuator;
    if (self->stop_motor) {
        self->stop_motor(self);
    }
    return true;
}

// --- Creación y destrucción ---

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
    free(dispenser);
}