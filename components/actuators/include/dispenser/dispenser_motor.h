#pragma once

#include "dispenser.h"
#include <stdbool.h>

void dispenser_motor_start(void* self_ptr);
void dispenser_motor_stop(void* self_ptr);
void dispenser_check_aspa_detected(DispenserActuator* self);
void dispenser_check_timeout(DispenserActuator* dispenser);