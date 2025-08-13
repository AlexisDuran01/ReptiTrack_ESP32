#pragma once
#include <stdbool.h>

// Inicializa el pin del LED (debe llamarse una vez al inicio)
void dispenser_led_init(void);

// Enciende el LED fijo
void dispenser_led_on(void);

// Apaga el LED
void dispenser_led_off(void);

// Inicia parpadeo del LED con el periodo especificado (en ms)
void dispenser_led_blink_start(int periodo_ms);

// Detiene el parpadeo del LED
void dispenser_led_blink_stop(void);

// Parpadeo especial para fetch (2 destellos rápidos)
void dispenser_led_pattern_fetch(void);

// Parpadeo especial para update (3 destellos rápidos)
void dispenser_led_pattern_update(void);

// Enciende o apaga el LED según si la hora está sincronizada
void dispenser_led_set_synced(bool synced);

void dispenser_led_pattern_update_async(void);

void dispenser_led_pattern_update_stop(void);