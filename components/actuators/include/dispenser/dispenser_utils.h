#pragma once

#include "dispenser.h"
#include <stdbool.h>

bool get_terrario_id_from_nvs(char *terrario_id_buf, size_t buf_len);
void imprimir_compartimientos_global(void);
bool dispenser_parse_compartments_json(const char* json_str, DispenserCompartments* compartimientos);