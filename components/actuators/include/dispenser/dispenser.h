#pragma once

#include <stdbool.h>
#include <time.h>
#include "actuator.h"

// --- Constantes ---
#define DISPENSER_MAX_COMPARTMENTS 5

#define DISPENSER_MAX_COMPARTMENTS 5
#define DISPENSER_PWM_CANAL        LEDC_CHANNEL_0      // Canal PWM del motor dispensador
#define DISPENSER_PWM_TIMER_ID     LEDC_TIMER_0        // Timer PWM usado
#define DISPENSER_PWM_MODO         LEDC_HIGH_SPEED_MODE// Modo de alta velocidad
#define DISPENSER_PWM_FRECUENCIA   20000               // Frecuencia PWM 20 kHz
#define DISPENSER_PWM_DUTY         205                 // Duty cycle (valor 0-255 para 8 bits)
#define DISPENSER_PWM_RESOLUCION   LEDC_TIMER_8_BIT    // Resolución PWM (8 bits)

#define DISPENSER_TIEMPO_FRENADO_MS    20                   // Tiempo de frenado suave (ms)
#define DISPENSER_TIEMPO_MAX_MOVIMIENTO_MS 1000              // Tiempo máximo permitido para un giro (ms)



// --- Estructuras ---
typedef struct {
    int id;
    time_t fecha_programada; // <-- Cambia a time_t
    bool dispensado;
    bool manual;
} DispenserCompartment;

typedef struct {
    DispenserCompartment compartments[DISPENSER_MAX_COMPARTMENTS];
    int count;
} DispenserCompartments;

typedef struct DispenserActuator {
    Actuator base;
    int pinIN3;
    int pinIN4;
    int pinENB;
    int pinSensorFC51;

    bool enMovimiento;
    int compartimientoActual;
    bool aspaDetectada;
    uint64_t tiempoInicioMovimiento;
    DispenserCompartments compartimientos;

    // Punteros a funciones para motor
    void (*start_motor)(void* self_ptr);
    void (*stop_motor)(void* self_ptr);
} DispenserActuator;

// --- Funciones principales ---
Actuator* dispenser_create(int pinIN3, int pinIN4, int pinENB, int pinSensorFC51);
void dispenser_destroy(Actuator* actuator);

bool dispenser_actuator_init(Actuator* actuator);
void dispenser_actuator_start(Actuator* actuator);
bool dispenser_actuator_fetch(Actuator* actuator);
bool dispenser_actuator_update(Actuator* actuator);
bool dispenser_actuator_deinit(Actuator* actuator);

void check_scheduled_compartments(DispenserActuator* dispenser);
