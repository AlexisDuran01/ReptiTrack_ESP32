#pragma once

#include <stdbool.h>
#include "esp_err.h"


#define NVS_NAMESPACE_FIRESTORE "database"
#define NVS_KEY_FIRESTORE "firestore_data"
#define FIRESTORE_BASE_URL_MAX_LEN 256
#define FIRESTORE_MAX_RESPONSE_LEN 1024



/**
 * @brief Estructura que almacena los IDs para Firestore
 */
typedef struct {
    char user_id[64];
    char terrario_id[64];
} firestore_t;


/**
 * @brief Inicializa cualquier estado interno para Firestore 
 */
void firestore_init(void);

/**
 * @brief Libera recursos internos si fuera necesario
 */
void firestore_deinit(void);

/**
 * @brief Indica si Firestore está inicializado correctamente
 * 
 * @return true si Firestore fue inicializado exitosamente, false en caso contrario
 */
bool firestore_is_initialized(void);

/**
 * @brief Guarda las credenciales de Firestore en NVS
 * 
 * @param data Puntero a la estructura firestore_t a guardar
 * @return esp_err_t Código de error ESP-IDF
 */
esp_err_t firestore_save(const firestore_t *data);

/**
 * @brief Carga las credenciales de Firestore desde NVS
 * 
 * @param data Puntero a la estructura firestore_t donde se cargará la info
 * @return esp_err_t Código de error ESP-IDF
 */
esp_err_t firestore_load(firestore_t *data);

/**
 * @brief Construye la base URL para Firestore usando los IDs almacenados en NVS
 * 
 * Ejemplo resultado:
 * "https://firestore.googleapis.com/v1/projects/reptitrack-946e0/databases/(default)/documents/usuarios/{user_id}/terrarios/{terrario_id}"
 * 
 * @return true si la URL fue construida exitosamente, false en caso contrario
 */
bool firestore_build_base_url(void);

/**
 * @brief Retorna la URL base construida (const char*)
 * 
 * @return puntero a la cadena con la URL base, NULL si no construida
 */
const char *firestore_get_base_url(void);

/**
 * @brief Construye una URL completa a Firestore combinando base + subruta
 * @param subpath Ruta relativa a partir de la base
 * @return char* URL completa (debe ser liberada con free), o NULL si falla
 */
char *firestore_build_url(const char *subpath);

/**
 * @brief Realiza una petición GET a un documento o subcolección de Firestore
 *
 * @param subpath Ruta relativa a partir de la URL base (por ejemplo: "sensores/temperatura")
 * @param outResponse Buffer para almacenar la respuesta JSON (debe tener tamaño max_len)
 * @param max_len Tamaño máximo del buffer outResponse
 * @return esp_err_t Código de error ESP-IDF
 */
esp_err_t firestore_get_document(const char *subpath, char **outResponse, size_t *out_len);

/**
 * @brief Crea un documento nuevo en Firestore dentro de la colección terrarios para el usuario dado
 * 
 * Usa user_id de firestore_t guardada en NVS
 * 
 * @param jsonPayload JSON en formato Firestore REST API para el nuevo documento
 * @param outResponse Buffer para almacenar la respuesta JSON
 * @param max_len Tamaño máximo del buffer outResponse
 * @return esp_err_t Código de error ESP-IDF
 */
esp_err_t firestore_create_document(const char *path, const char *jsonPayload, char *outResponse, size_t max_len);

/**
 * @brief Reemplaza completamente un documento en Firestore (usa PUT)
 * 
 * @param path Ruta relativa desde la base (ej: "sensores/temp123")
 * @param jsonPayload JSON completo del documento nuevo
 * @param outResponse Buffer para almacenar la respuesta JSON
 * @param max_len Tamaño máximo del buffer outResponse
 * @return esp_err_t Código de error ESP-IDF
 */
esp_err_t firestore_overwrite_document(const char *path, const char *jsonPayload, char *outResponse, size_t max_len);

/**
 * @brief Actualiza uno o más campos de un documento en Firestore (usa PATCH)
 * 
 * @param path Ruta relativa desde la base (ej: "sensores/temp123")
 * @param jsonPayload JSON SOLO con los campos a modificar
 * @param outResponse Buffer para almacenar la respuesta JSON
 * @param max_len Tamaño máximo del buffer outResponse
 * @return esp_err_t Código de error ESP-IDF
 */
esp_err_t firestore_update_document(const char *subpath, const char *jsonPayload, const char *updateMask, char *outResponse, size_t max_len);


/**
 * @brief Verifica si hay conectividad con Firestore y al menos un terrario
 * 
 * @return true si hay al menos un documento de terrario, false si falla o está vacío
 */
bool firestore_check_connectivity(void);