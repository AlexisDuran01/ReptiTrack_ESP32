#include "dispenser/dispenser_motor.h"
#include "dispenser/dispenser.h"
#include <esp_log.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "rtc_time.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "esp_timer.h"
#include "dispenser/dispenser_led.h"
#include "dispenser/dispenser_utils.h"

#define TAG "dispenser_motor"

#define LED_GPIO 16

extern SemaphoreHandle_t g_compartimientos_mutex;
extern DispenserCompartments g_compartimientos_global;
extern SemaphoreHandle_t g_fetch_pause_semaphore;
extern bool ronda_finalizada;
bool firestore_fetch_update_dispensed(struct DispenserActuator* self, int compartment_id);

#define FIRESTORE_UPDATE_RETRIES 5
#define FIRESTORE_UPDATE_RETRY_DELAY_MS 500


// Implementación de start_motor
void dispenser_motor_start(void* self_ptr) {
	
	dispenser_led_blink_start(100); // Parpadeo rápido mientras gira
	
    DispenserActuator* self = (DispenserActuator*)self_ptr;

    gpio_set_level(self->pinIN3, 1);
    gpio_set_level(self->pinIN4, 0);

    ledc_set_duty(DISPENSER_PWM_MODO, DISPENSER_PWM_CANAL, DISPENSER_PWM_DUTY);
    ledc_update_duty(DISPENSER_PWM_MODO, DISPENSER_PWM_CANAL);

    self->enMovimiento = true;
    self->aspaDetectada = false;

    ESP_LOGI(TAG, "Motor iniciado");
}

// Implementación de stop_motor
void dispenser_motor_stop(void* self_ptr) {
    DispenserActuator* self = (DispenserActuator*)self_ptr;
	dispenser_led_blink_stop();
    ESP_LOGI(TAG, "Deteniendo motor suavemente...");

    for (int duty = DISPENSER_PWM_DUTY; duty > 0; duty -= 5) {
        ledc_set_duty(DISPENSER_PWM_MODO, DISPENSER_PWM_CANAL, duty);
        ledc_update_duty(DISPENSER_PWM_MODO, DISPENSER_PWM_CANAL);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    gpio_set_level(self->pinIN3, 0);
    gpio_set_level(self->pinIN4, 0);
    ledc_set_duty(DISPENSER_PWM_MODO, DISPENSER_PWM_CANAL, 0);
    ledc_update_duty(DISPENSER_PWM_MODO, DISPENSER_PWM_CANAL);

    vTaskDelay(pdMS_TO_TICKS(DISPENSER_TIEMPO_FRENADO_MS));
    ESP_LOGI(TAG, "Motor detenido.");

    // Actualización de estado local y global
    if (xSemaphoreTake(g_compartimientos_mutex, pdMS_TO_TICKS(100))) {
        if (self->compartimientoActual >= 0 && self->compartimientoActual < self->compartimientos.count) {
            // Actualizar localmente
            self->compartimientos.compartments[self->compartimientoActual].dispensado = true;

            // Actualizar globalmente
            if (self->compartimientoActual < g_compartimientos_global.count) {
                g_compartimientos_global.compartments[self->compartimientoActual].dispensado = true;

                int compart_id_real = g_compartimientos_global.compartments[self->compartimientoActual].id;

                // Tomar semáforo para pausar fetch concurrente
                bool sem_taken = false;
                if (g_fetch_pause_semaphore != NULL) {
    				if (xSemaphoreTake(g_fetch_pause_semaphore, pdMS_TO_TICKS(10000)) == pdTRUE) { // <-- Aumenta el timeout aquí
                        ESP_LOGI(TAG, "fetch_task bloqueada para actualizacion Firestore");
                        sem_taken = true;
                    } else {
                        ESP_LOGW(TAG, "No pudo bloquear fetch_task antes de actualizar Firestore");
                    }
                }

	
				bool resultado = false;
				for (int intento = 1; intento <= FIRESTORE_UPDATE_RETRIES; intento++) {
				    resultado = firestore_fetch_update_dispensed(self, compart_id_real);
				    if (resultado) {
				        break;
				    } else {
				        ESP_LOGW(TAG, "Reintento %d/%d: Error actualizando Firestore para compartimiento ID %d", 
				                 intento, FIRESTORE_UPDATE_RETRIES, compart_id_real);
				        vTaskDelay(pdMS_TO_TICKS(FIRESTORE_UPDATE_RETRY_DELAY_MS));
				    }
				}			
				
				if (!resultado) {
			    ESP_LOGE(TAG, "Error actualizando Firestore para compartimiento ID %d", compart_id_real);
			    dispenser_led_set_synced(false); // LED apagado por posible error de sincronización
			} else {
			    // Si el update fue exitoso, valida la hora
			    time_t now;
			    bool hora_valida = false;
			    if (rtc_time_get_current(&now) != ESP_OK) {
			        ESP_LOGW(TAG, "No se pudo obtener hora tras update, sincronizando...");
			        rtc_time_sync_with_timezone("America/Mexico_City");
			        dispenser_led_set_synced(false); // LED apagado mientras no hay hora válida
			    } else {
			        struct tm tm_now;
			        gmtime_r(&now, &tm_now);
			        if (tm_now.tm_year + 1900 < 2025) {
			            ESP_LOGW(TAG, "Hora RTC inválida tras update (%d), sincronizando...", tm_now.tm_year + 1900);
			            rtc_time_sync_with_timezone("America/Mexico_City");
			            dispenser_led_set_synced(false); // LED apagado mientras no hay hora válida
			        } else {
			            ESP_LOGI(TAG, "Hora RTC valida tras update: %04d-%02d-%02d %02d:%02d:%02d",
			                tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
			                tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
			            hora_valida = true;
			           // dispenser_nvs_limpiar_dispensado(compart_id_real);
			            dispenser_led_set_synced(true); // LED encendido fijo si la hora es válida
			        }
			    }
			}

                // Liberar semáforo fetch
                if (sem_taken) {
                    xSemaphoreGive(g_fetch_pause_semaphore);
                    ESP_LOGI(TAG, "fetch_task reactivada despues de actualizacion Firestore");
                }
            } else {
                ESP_LOGE(TAG, "Indice compartimientoActual fuera de rango");
            }
        } else {
            ESP_LOGE(TAG, "CompartimientoActual inválido para actualizar Firestore");
        }
        xSemaphoreGive(g_compartimientos_mutex);
    } else {
        ESP_LOGW(TAG, "No se pudo tomar mutex para actualizar compartimientos globales");
    }

    self->enMovimiento = false;
    self->aspaDetectada = false; // Deja el estado limpio
    self->compartimientoActual = -1;
}

// Detección de aspa
void dispenser_check_aspa_detected(DispenserActuator* self) {
    if (!self->enMovimiento) return;

    bool aspaDetectada = gpio_get_level(self->pinSensorFC51) == 0;

    if (aspaDetectada && !self->aspaDetectada) {
        ESP_LOGI(TAG, "Aspa detectada. Deteniendo motor.");
        self->aspaDetectada = true;
        self->enMovimiento = false; // Evita doble stop
        if (self->stop_motor) {
            self->stop_motor(self);
        }
    }
    
        dispenser_led_blink_stop(); // Detiene parpadeo

}

// Timeout de movimiento
void dispenser_check_timeout(DispenserActuator* dispenser) {
    if (!dispenser || !dispenser->enMovimiento || dispenser->aspaDetectada) return;

    uint64_t now_ms = esp_timer_get_time() / 1000;

    if ((now_ms - dispenser->tiempoInicioMovimiento) > DISPENSER_TIEMPO_MAX_MOVIMIENTO_MS) {
        ESP_LOGW(TAG, "Tiempo maximo de giro excedido! Deteniendo motor por seguridad");
        
	        dispenser->enMovimiento = false; // Evita doble stop
        if (dispenser->stop_motor) {
            dispenser->stop_motor(dispenser);
        } else {
            ESP_LOGE(TAG, "No se pudo detener el motor: stop_motor no definido");
        }
        // No cambies aspaDetectada ni compartimientoActual aquí, lo hace stop_motor
    }
}