#ifndef DISPENSER_H
#define DISPENSER_H

#include <stdbool.h>
#include <time.h>
#include "actuator.h"   // Incluir para heredar

#define DISPENSER_MAX_COMPARTMENTS 5
#define DISPENSER_PWM_CANAL        LEDC_CHANNEL_0      // Canal PWM del motor dispensador
#define DISPENSER_PWM_TIMER_ID     LEDC_TIMER_0        // Timer PWM usado
#define DISPENSER_PWM_MODO         LEDC_HIGH_SPEED_MODE// Modo de alta velocidad
#define DISPENSER_PWM_FRECUENCIA   20000               // Frecuencia PWM 20 kHz
#define DISPENSER_PWM_DUTY         220                 // Duty cycle (valor 0-255 para 8 bits)
#define DISPENSER_PWM_RESOLUCION   LEDC_TIMER_8_BIT    // Resolución PWM (8 bits)

#define DISPENSER_TIEMPO_FRENADO_MS    20                   // Tiempo de frenado suave (ms)
#define DISPENSER_TIEMPO_MAX_MOVIMIENTO_MS 800              // Tiempo máximo permitido para un giro (ms)
typedef struct {
    char id[16];
    time_t fecha_programada;
    bool dispensado;
    bool manual;
} DispenserCompartment;

typedef struct {
    DispenserCompartment compartments[DISPENSER_MAX_COMPARTMENTS];
    int count;
} DispenserCompartments;

typedef struct {
    Actuator base;  // Estructura base "heredada"

    DispenserCompartments compartimientos;

    int pinIN3;
    int pinIN4;
    int pinENB;
    int pinSensorFC51;
    
    bool enMovimiento;
    unsigned long tiempoInicioMovimiento;  // Para medir millis en ESP-IDF usa `esp_timer_get_time() / 1000`


    bool aspaDetectada;
    int compartimientoActual;
	    
    void (*start_motor)(void* self);
    void (*stop_motor)(void* self);

} DispenserActuator;

// Ahora dispenser_create devuelve Actuator*, para que el registro sea directo
Actuator* dispenser_create(int pinIN3, int pinIN4, int pinENB, int pinSensorFC51);

void dispenser_destroy(Actuator* actuator);

// Métodos para usar en Actuator (init, start, fetch, update, deinit)
bool dispenser_actuator_init(Actuator* actuator);
void dispenser_actuator_start(Actuator* actuator);
bool dispenser_actuator_fetch(Actuator* actuator);
bool dispenser_actuator_update(Actuator* actuator);
bool dispenser_actuator_deinit(Actuator* actuator);
static void dispenser_task(void* pvParameter) ;
static void fetch_task(void* pvParameter);


#endif // DISPENSER_H
