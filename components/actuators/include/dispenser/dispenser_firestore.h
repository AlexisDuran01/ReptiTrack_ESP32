#pragma once

#include "dispenser.h"
#include <stdbool.h>

bool firestore_fetch_update_dispensed(DispenserActuator* self, int compartment_id);
bool dispenser_actuator_fetch(Actuator* actuator);