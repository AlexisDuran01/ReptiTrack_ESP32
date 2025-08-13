#include "dispenser/dispenser_firestore.h"
#include "dispenser/dispenser_utils.h"
#include "dispenser/dispenser.h"
#include "firestore.h"
#include "nvs_utils.h"
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "rtc_time.h"
#include "dispenser/dispenser_led.h"


#define TAG "dispenser_firestore"
#define TERRARIO_ID_MAX_LEN 64

// Variables globales externas
extern DispenserCompartments g_compartimientos_global;
extern SemaphoreHandle_t g_compartimientos_mutex;
extern SemaphoreHandle_t g_fetch_pause_semaphore;

bool firestore_fetch_update_dispensed(DispenserActuator* self, int compartment_id) {
	
	dispenser_led_off();
	dispenser_led_pattern_update_async();
    if (!self || compartment_id < 0) {
        ESP_LOGE(TAG, "Parámetros inválidos: self=%p, compartment_id=%d", self, compartment_id);
        return false;
    }
    
    ESP_LOGI(TAG, "Iniciando actualizacion de dispensado para compartimiento_id=%d", compartment_id);
	
	  dispenser_led_pattern_fetch();

    // Cargar datos de Firestore desde NVS
    firestore_t data = {0};
    esp_err_t err = firestore_load(&data);
    if (err != ESP_OK || strlen(data.user_id) == 0) {
        return false;
    }

    // Obtener terrario_id desde NVS
    char terrario_id[TERRARIO_ID_MAX_LEN] = {0};
    if (!get_terrario_id_from_nvs(terrario_id, sizeof(terrario_id))) {
        return false;
    }

    // Construir path del documento
    char get_subpath[256] = {0};
    snprintf(get_subpath, sizeof(get_subpath),
             "terrarios/%s/dispositivos/esp01/actuadores/dispensador",
             terrario_id);

    // Obtener documento de Firestore
    char *document_json = NULL;
    size_t json_len = 0;
    err = firestore_get_document(get_subpath, &document_json, &json_len);
    if (err != ESP_OK || !document_json) {
        if (document_json) free(document_json);
        return false;
    }

    // Parsear JSON
    cJSON *root = cJSON_Parse(document_json);
    free(document_json);
    if (!root) {
        return false;
    }

    // Navegar estructura JSON
    cJSON *fields = cJSON_GetObjectItem(root, "fields");
    cJSON *programaciones = fields ? cJSON_GetObjectItem(fields, "programaciones") : NULL;
    cJSON *arrayValue = programaciones ? cJSON_GetObjectItem(programaciones, "arrayValue") : NULL;
    cJSON *values = arrayValue ? cJSON_GetObjectItem(arrayValue, "values") : NULL;

    if (!values || !cJSON_IsArray(values)) {
        cJSON_Delete(root);
        return false;
    }

    // Buscar compartimiento específico
    bool found = false;
    int arr_size = cJSON_GetArraySize(values);

    for (int i = 0; i < arr_size; i++) {
        cJSON *comp_item = cJSON_GetArrayItem(values, i);
        if (!comp_item) continue;

        cJSON *mapValue = cJSON_GetObjectItem(comp_item, "mapValue");
        cJSON *comp_fields = mapValue ? cJSON_GetObjectItem(mapValue, "fields") : NULL;
        if (!comp_fields) continue;

        // Verificar ID del compartimiento
        cJSON *id_obj = cJSON_GetObjectItem(comp_fields, "id");
        if (!id_obj) continue;
        cJSON *id_value = cJSON_GetObjectItem(id_obj, "integerValue");
        if (!id_value || !cJSON_IsString(id_value)) continue;

        int current_id = atoi(id_value->valuestring);
        
        if (current_id == compartment_id) {
            // Actualizar campo 'dispensado'
            cJSON *dispensado_obj = cJSON_GetObjectItem(comp_fields, "dispensado");
            if (!dispensado_obj) {
                dispensado_obj = cJSON_CreateObject();
                cJSON_AddItemToObject(comp_fields, "dispensado", dispensado_obj);
            }
            
            cJSON *bool_value = cJSON_GetObjectItem(dispensado_obj, "booleanValue");
            if (!bool_value) {
                cJSON_AddBoolToObject(dispensado_obj, "booleanValue", true);
            } else {
                cJSON_SetBoolValue(bool_value, true);
            }
            
            found = true;
            break;
        }
    }

    if (!found) {
        cJSON_Delete(root);
        return false;
    }

    // Preparar JSON para PATCH - Usamos el root completo ya modificado
    char *modified_json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!modified_json_str) {
        return false;
    }

    // Construir path para PATCH terrario
    char patch_subpath[256] = {0};
    snprintf(patch_subpath, sizeof(patch_subpath),
             "terrarios/%s/dispositivos/esp01/actuadores/dispensador",
             terrario_id);

    // Campo(s) a modificar (updateMask)
    const char *updateMask = "programaciones";

    // Buffer para respuesta
    char respuesta[2048];


    // Llamar a la función actualizada con updateMask
    err = firestore_update_document(patch_subpath, modified_json_str, updateMask, respuesta, sizeof(respuesta));
    free(modified_json_str);
    dispenser_led_pattern_update_stop();
    bool synced = false;
	bool hora_valida = false;
	if (rtc_time_is_synchronized(&synced) == ESP_OK && synced) {
	    time_t now;
	    if (rtc_time_get_current(&now) == ESP_OK) {
	        struct tm tm_now;
	        gmtime_r(&now, &tm_now);
	        if (tm_now.tm_year + 1900 >= 2025) {
	            hora_valida = true;
	        }
	    }
	}
	dispenser_led_set_synced(hora_valida);

    return (err == ESP_OK);
}

bool dispenser_actuator_fetch(Actuator* actuator) {

    if (!firestore_is_initialized()) {
        ESP_LOGE(TAG, "Firestore no inicializado");
        return false;
    }
      
    // Leer terrario_id guardado en NVS
    char terrario_id[TERRARIO_ID_MAX_LEN];
    if (!get_terrario_id_from_nvs(terrario_id, sizeof(terrario_id))) {
        ESP_LOGE(TAG, "No se pudo obtener terrario_id de NVS");
        return false;
    }

    // Construir subpath relativo para Firestore usando terrario_id
    char subpath[128];
    snprintf(subpath, sizeof(subpath), "terrarios/%s/dispositivos/esp01/actuadores/dispensador", terrario_id);

    char *full_url = firestore_build_url(subpath);
    if (!full_url) {
        ESP_LOGE(TAG, "No se pudo construir URL para Firestore");
        return false;
    }

    // Usar firestore_get_document para obtener JSON
    char *response_json = NULL;
    size_t response_len = 0;
    esp_err_t err = firestore_get_document(subpath, &response_json, &response_len);
   	
   	dispenser_led_pattern_fetch();
    free(full_url);

    if (err != ESP_OK || !response_json) {
        ESP_LOGE(TAG, "Error al obtener documento Firestore");
        if (response_json) free(response_json);
        return false;
    }

    // Parsear JSON Firestore para extraer el array "programaciones"
    cJSON* root = cJSON_Parse(response_json);
    free(response_json);
    if (!root) {
        ESP_LOGE(TAG, "JSON Firestore invalido");
        return false;
    }

    cJSON* fields = cJSON_GetObjectItem(root, "fields");
    if (!fields) {
        ESP_LOGE(TAG, "No se encontro 'fields' en JSON");
        cJSON_Delete(root);
        return false;
    }

    cJSON* programaciones = cJSON_GetObjectItem(fields, "programaciones");
    if (!programaciones) {
        ESP_LOGE(TAG, "No se encontro 'programaciones' en 'fields'");
        cJSON_Delete(root);
        return false;
    }

    cJSON* arrayValue = cJSON_GetObjectItem(programaciones, "arrayValue");
    if (!arrayValue) {
        ESP_LOGE(TAG, "No se encontro 'arrayValue' en 'programaciones'");
        cJSON_Delete(root);
        return false;
    }

    cJSON* values = cJSON_GetObjectItem(arrayValue, "values");
    if (!values || !cJSON_IsArray(values)) {
        ESP_LOGE(TAG, "No se encontro arreglo 'values' en 'arrayValue'");
        cJSON_Delete(root);
        return false;
    }

    cJSON* new_root = cJSON_CreateObject();
    cJSON_AddItemToObject(new_root, "compartments", cJSON_Duplicate(values, 1));
    cJSON_Delete(root);

    char* compartments_json_str = cJSON_PrintUnformatted(new_root);
    cJSON_Delete(new_root);

    if (!compartments_json_str) {
        ESP_LOGE(TAG, "No se pudo crear string JSON de compartimientos");
        return false;
    }

    DispenserCompartments new_compartments;
    memset(&new_compartments, 0, sizeof(new_compartments));
    bool success = dispenser_parse_compartments_json(compartments_json_str, &new_compartments);
    free(compartments_json_str);

    if (!success) {
        ESP_LOGE(TAG, "Error parseando compartimientos");
        return false;
    }

    if (xSemaphoreTake(g_compartimientos_mutex, pdMS_TO_TICKS(1000))) {
        g_compartimientos_global = new_compartments;
        xSemaphoreGive(g_compartimientos_mutex);
        ESP_LOGI(TAG, "Compartimientos actualizados desde Firestore");
        rtc_time_print_current();
        
            bool synced = false;
		    bool hora_valida = false;
		    if (rtc_time_is_synchronized(&synced) == ESP_OK && synced) {
		        time_t now;
		        if (rtc_time_get_current(&now) == ESP_OK) {
		            struct tm tm_now;
		            gmtime_r(&now, &tm_now);
		            if (tm_now.tm_year + 1900 >= 2025) {
		                hora_valida = true;
		            }
		        }
		    }
		    dispenser_led_set_synced(hora_valida);
		    
        return true;
    } else {
        ESP_LOGW(TAG, "No se pudo tomar mutex para actualizar compartimientos");
        return false;
    }
}