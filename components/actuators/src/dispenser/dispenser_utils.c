#include "dispenser/dispenser_utils.h"
#include "dispenser/dispenser.h"
#include <esp_log.h>
#include <string.h>
#include "nvs_utils.h"
#include "cJSON.h"
#include "rtc_time.h"
#include "freertos/FreeRTOS.h"

#define TAG "dispenser_utils"

bool get_terrario_id_from_nvs(char *terrario_id_buf, size_t buf_len) {
    if (!terrario_id_buf || buf_len == 0) return false;

    memset(terrario_id_buf, 0, buf_len);
    size_t actual_len = 0;

    esp_err_t err = nvs_utils_load_blob("terrarios", "id", terrario_id_buf, buf_len, &actual_len);
    if (err != ESP_OK || actual_len == 0 || actual_len > buf_len) {
        ESP_LOGE(TAG, "Error cargando terrario_id desde NVS: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

void imprimir_compartimientos_global(void) {
    extern DispenserCompartments g_compartimientos_global;
    extern SemaphoreHandle_t g_compartimientos_mutex;

    if (xSemaphoreTake(g_compartimientos_mutex, pdMS_TO_TICKS(10))) {
        ESP_LOGI(TAG, "Imprimiendo compartimientos globales. Total: %d", g_compartimientos_global.count);

        for (int i = 0; i < g_compartimientos_global.count; i++) {
            DispenserCompartment* comp = &g_compartimientos_global.compartments[i];

            char fecha_str[32];
            struct tm tm_utc;
            gmtime_r(&comp->fecha_programada, &tm_utc);
            strftime(fecha_str, sizeof(fecha_str), "%Y-%m-%d %H:%M:%S", &tm_utc);

            ESP_LOGI(TAG, "Compartimiento %d:", comp->id);
            ESP_LOGI(TAG, "  Fecha programada: %s", fecha_str);
            ESP_LOGI(TAG, "  Dispensado: %s", comp->dispensado ? "Si" : "No");
            ESP_LOGI(TAG, "  Manual: %s", comp->manual ? "Si" : "No");
        }

        xSemaphoreGive(g_compartimientos_mutex);
    } else {
        ESP_LOGW(TAG, "No se pudo tomar mutex para imprimir compartimientos globales");
    }
}

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

        cJSON* map_value = cJSON_GetObjectItem(item, "mapValue");
        if (!map_value || !cJSON_IsObject(map_value)) {
            ESP_LOGW(TAG, "No se encontró 'mapValue' para compartimiento índice %d", i);
            continue;
        }

        cJSON* fields = cJSON_GetObjectItem(map_value, "fields");
        if (!fields || !cJSON_IsObject(fields)) {
            ESP_LOGW(TAG, "No se encontró 'fields' en 'mapValue' para índice %d", i);
            continue;
        }

        cJSON* id_obj = cJSON_GetObjectItem(fields, "id");
        cJSON* fecha_obj = cJSON_GetObjectItem(fields, "fechaProgramada");
        cJSON* dispensado_obj = cJSON_GetObjectItem(fields, "dispensado");
        cJSON* manual_obj = cJSON_GetObjectItem(fields, "manual");

        if (!id_obj || !fecha_obj || !dispensado_obj || !manual_obj) {
            ESP_LOGW(TAG, "Faltan campos en JSON para compartimiento índice %d", i);
            continue;
        }

        cJSON* id = cJSON_GetObjectItem(id_obj, "integerValue");
        cJSON* timestamp = cJSON_GetObjectItem(fecha_obj, "timestampValue");
        cJSON* dispensado = cJSON_GetObjectItem(dispensado_obj, "booleanValue");
        cJSON* manual = cJSON_GetObjectItem(manual_obj, "booleanValue");

        if (!cJSON_IsString(id) || !cJSON_IsString(timestamp) ||
            (!cJSON_IsBool(dispensado) && dispensado == NULL) ||
            (!cJSON_IsBool(manual) && manual == NULL)) {
            ESP_LOGW(TAG, "Campos inválidos para compartimiento índice %d", i);
            continue;
        }

		 comp->id = atoi(id->valuestring);
		
		    // Convertir el string ISO8601 a struct tm y luego a time_t (UTC)
		    struct tm tm_utc;
		    if (rtc_time_parse_iso8601_to_tm(timestamp->valuestring, &tm_utc) == ESP_OK) {
		        comp->fecha_programada = timegm(&tm_utc);
		    } else {
		        ESP_LOGW(TAG, "Fecha inválida para id %d: %s", comp->id, timestamp->valuestring);
		        comp->fecha_programada = 0;
		    }
		
		    comp->dispensado = cJSON_IsTrue(dispensado);
		    comp->manual = cJSON_IsTrue(manual);
    }

    compartimientos->count = count;
    cJSON_Delete(root);

    return true;
}