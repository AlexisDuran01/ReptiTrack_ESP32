#include "dispenser/dispenser_led.h"
#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define DISPENSER_LED_GPIO 16

static TaskHandle_t blink_task_handle = NULL;

static TaskHandle_t led_pattern_update_task_handle = NULL;


// Tarea para parpadeo continuo
static void dispenser_led_blink_task(void *pvParameter) {
    int periodo_ms = (int)pvParameter;
    while (1) {
        gpio_set_level(DISPENSER_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(periodo_ms / 2));
        gpio_set_level(DISPENSER_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(periodo_ms / 2));
    }
}

void dispenser_led_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DISPENSER_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(DISPENSER_LED_GPIO, 0);
}

void dispenser_led_on(void) {
    if (blink_task_handle) {
        vTaskDelete(blink_task_handle);
        blink_task_handle = NULL;
    }
    gpio_set_level(DISPENSER_LED_GPIO, 1);
}

void dispenser_led_off(void) {
    if (blink_task_handle) {
        vTaskDelete(blink_task_handle);
        blink_task_handle = NULL;
    }
    gpio_set_level(DISPENSER_LED_GPIO, 0);
}

void dispenser_led_blink_start(int periodo_ms) {
    if (blink_task_handle) {
        vTaskDelete(blink_task_handle);
        blink_task_handle = NULL;
    }
    xTaskCreate(dispenser_led_blink_task, "led_blink", 1024, (void*)periodo_ms, 5, &blink_task_handle);
}

void dispenser_led_blink_stop(void) {
    if (blink_task_handle) {
        vTaskDelete(blink_task_handle);
        blink_task_handle = NULL;
    }
    gpio_set_level(DISPENSER_LED_GPIO, 0);
}

// Parpadeo especial para fetch (2 destellos rápidos)
void dispenser_led_pattern_fetch(void) {
    for (int i = 0; i < 3; i++) {
        gpio_set_level(DISPENSER_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(400));
        gpio_set_level(DISPENSER_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
}

// Parpadeo para update (3 destellos rápidos)
static void dispenser_led_pattern_update_task(void *pvParameter) {
    while (1) {
        gpio_set_level(DISPENSER_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
        gpio_set_level(DISPENSER_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void dispenser_led_pattern_update_async(void) {
    if (led_pattern_update_task_handle == NULL) {
        xTaskCreate(dispenser_led_pattern_update_task, "led_update_pattern", 1024, NULL, 5, &led_pattern_update_task_handle);
    }
}

// Si quieres forzar la eliminación antes de que termine:
void dispenser_led_pattern_update_stop(void) {
    if (led_pattern_update_task_handle != NULL) {
        vTaskDelete(led_pattern_update_task_handle);
        led_pattern_update_task_handle = NULL;
        gpio_set_level(DISPENSER_LED_GPIO, 0); // Apaga el LED
    }
}

// Enciende o apaga el LED según si la hora está sincronizada
void dispenser_led_set_synced(bool synced) {
    if (synced) {
        dispenser_led_on();
    } else {
        dispenser_led_off();
    }
}