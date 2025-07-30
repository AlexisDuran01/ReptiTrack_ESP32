// Cabeceras necesarias para trabajar con NVS y logs
#include "nvs_utils.h"     // Declaraciones de las funciones utilitarias (header personalizado)
#include "nvs_flash.h"     // Inicialización de la partición NVS
#include "nvs.h"           // Funciones principales para manejar la NVS
#include "esp_log.h"       // Logging con niveles (INFO, ERROR, etc.)
#include <stdio.h>         // printf
#include <string.h>        // strlen
#include <inttypes.h>  //  Es para imprimir bien tipos como uint32_t, sin preocuparte si son unsigned int o unsigned long

/*
 * Archivo de implementación (source) para el módulo nvs_utils.
 * 
 * - Contiene la implementación real de las funciones declaradas en nvs_utils.h.
 * 
 * - Aquí se escribe el código que realiza las operaciones sobre NVS,
 *   incluyendo la lógica de abrir la NVS, leer, escribir y borrar datos.
 * 
 * - Este archivo se compila y se enlaza para formar parte del binario final.
 * 
 * - Está en 'src/' para separar la implementación del header,
 *   facilitando organización y mantenibilidad.
 * 
 * - Para usar estas funciones desde otro archivo, se incluye el header (.h)
 *   y se enlaza con este archivo compilado (.c).
 */


// Etiqueta usada para identificar los mensajes de log de este módulo
static const char *TAG = "nvs_utils";


/**
 * Guarda un bloque de datos binarios (blob) en la NVS.
 * Esto es útil para guardar configuraciones, estados u objetos serializados.
 */
esp_err_t nvs_utils_save_blob(const char *namespace, const char *key, const void *data, size_t len) {

    // Verifica que ninguno de los parámetros de entrada sea NULL,
    // ya que intentar operar con punteros nulos causaría errores.
    if (!namespace || !key || !data) {
        // Imprime mensaje de error indicando parámetros inválidos
        printf("Error: parámetros nulos en save_blob\n");
        // Retorna un código de error estándar indicando argumento inválido
        return ESP_ERR_INVALID_ARG;
    }

    // Comprueba que la longitud de 'namespace' y 'key' no supere el máximo permitido (15 caracteres)
    // Esto es un requisito de la implementación de NVS para claves y namespaces.
    if (strlen(namespace) > 15 || strlen(key) > 15) {
        // Imprime mensaje de error indicando que la longitud es demasiado grande
        printf("Error: 'namespace' o 'key' exceden los 15 caracteres permitidos por NVS.\n");
        // Retorna error indicando argumento inválido
        return ESP_ERR_INVALID_ARG;
    }

    // Log informativo que indica que se va a guardar un blob,
    // mostrando el namespace, la clave y el tamaño de los datos a guardar.
    ESP_LOGI(TAG, "Guardando blob en NVS [%s/%s] (%u bytes)", namespace, key, (unsigned)len);

    // Declaración del handle que representará la sesión de acceso a NVS
    nvs_handle_t nvs;

    // Abre el espacio de nombres en modo lectura y escritura.
    // Si no existe, se crea.
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &nvs);

    // Verifica si la apertura fue exitosa
    if (err != ESP_OK) {
        // En caso de error, registra un mensaje con la descripción del error
        ESP_LOGE(TAG, "Error abriendo NVS para guardar: %s", esp_err_to_name(err));
        // Retorna el código de error recibido
        return err;
    }

    // Intenta escribir el blob de datos en la NVS usando la clave proporcionada
    err = nvs_set_blob(nvs, key, data, len);

    // Si la escritura fue exitosa
    if (err == ESP_OK) {
        // Confirma la escritura para que los datos queden persistidos en flash
        nvs_commit(nvs);

        // Registra un mensaje informando que la operación fue exitosa
        ESP_LOGI(TAG, "Blob guardado correctamente en [%s/%s]", namespace, key);
    } else {
        // En caso de error al escribir, registra un mensaje con el código de error
        ESP_LOGE(TAG, "Error guardando blob: %s", esp_err_to_name(err));
    }

    // Cierra la sesión con NVS para liberar recursos
    nvs_close(nvs);

    // Retorna el resultado final de la operación (éxito o código de error)
    return err;
}



// Función que carga un bloque de datos binarios (blob) desde la NVS

esp_err_t nvs_utils_load_blob(const char *namespace, const char *key, void *data, size_t max_len, size_t *actual_len) {
    
    // Verifica que ninguno de los parámetros de entrada sea NULL,
    // ya que intentar operar con punteros nulos causaría errores.
    if (!namespace || !key || !data) {
        // Imprime mensaje de error indicando parámetros inválidos
        printf("Error: parámetros nulos en save_blob\n");
        // Retorna un código de error estándar indicando argumento inválido
        return ESP_ERR_INVALID_ARG;
    }
    
    
    // Validación: comprobar que 'namespace' y 'key' no excedan los 15 caracteres permitidos
    if (strlen(namespace) > 15 || strlen(key) > 15) {
        // Si la longitud es mayor a 15, imprime error y devuelve código de argumento inválido
        printf("Error: 'namespace' o 'key' exceden los 15 caracteres permitidos por NVS.\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Log informativo indicando que se intentará leer un blob de NVS en el namespace y clave indicados
    ESP_LOGI(TAG, "Leyendo blob de NVS [%s/%s]", namespace, key);

    // Declaración del handle para abrir el espacio de nombres NVS
    nvs_handle_t nvs;

    // Abrir el espacio de nombres en modo solo lectura
    esp_err_t err = nvs_open(namespace, NVS_READONLY, &nvs);
    
    // Validar si la apertura fue exitosa
    if (err != ESP_OK) {
        // Si falla, registrar error con el código retornado y devolverlo
        ESP_LOGE(TAG, "Error abriendo NVS para leer: %s", esp_err_to_name(err));
        return err;
    }

    // Variable para almacenar el tamaño requerido para el blob
    size_t required_size = 0;

    // Consulta el tamaño del blob asociado a la clave sin leer el contenido (data = NULL)
    // Esto permite saber cuánto espacio se necesita para leer el dato completo
    err = nvs_get_blob(nvs, key, NULL, &required_size);

    // Si la consulta fue exitosa y el tamaño requerido cabe en el buffer proporcionado
    if (err == ESP_OK && required_size <= max_len) {
        // Leer el blob de datos en el buffer 'data' y actualizar 'required_size' con el tamaño leído
        err = nvs_get_blob(nvs, key, data, &required_size);

        // Si el puntero 'actual_len' no es NULL, almacenar en él el tamaño real leído
        if (actual_len) *actual_len = required_size;

	// 1. Detecta si el valor leído es una cadena de texto válida
	bool is_text = ((char*)data)[required_size - 1] == '\0' &&
	               memchr(data, '\0', required_size - 1) == NULL;
	
	if (is_text) {
	    // Si es una cadena terminada en nulo sin bytes nulos intermedios, se imprime como texto
	    ESP_LOGI(TAG, "Valor leido de NVS [%s/%s]: \"%s\"", namespace, key, (char*)data);
	} 
	// 2. Detecta si es un bool simple de 1 byte
	else if (required_size == 1) {
	    uint8_t val = ((uint8_t*)data)[0];
	    ESP_LOGI(TAG, "Valor leido de NVS [%s/%s]: (bool) %s", namespace, key, val ? "true" : "false");
	}
	// 3. Detecta si es un bool/int de 4 bytes (ej: 0x01 00 00 00 o 0x00 00 00 00)
	else if (required_size == 4) {
	    uint32_t val = *(uint32_t*)data;
	    if (val == 0 || val == 1) {
	        ESP_LOGI(TAG, "Valor leido de NVS [%s/%s]: (int-bool) %s", namespace, key, val ? "true" : "false");
	    } else {
			ESP_LOGI(TAG, "Valor leido de NVS [%s/%s]: (uint32) %" PRIu32, namespace, key, val);
	    }
	}
	// 4. Si no es interpretable como texto o bool, lo imprime en formato hexadecimal parcial
	else {
	    // Muestra los primeros bytes como hex (máximo 10 para no saturar logs)
	    char hex[64] = {0};
	    size_t len = required_size > 10 ? 10 : required_size;
	    for (size_t i = 0; i < len; ++i) {
	        sprintf(hex + i * 3, "%02X ", ((uint8_t*)data)[i]);
	    }
	    ESP_LOGI(TAG, "Valor leido de NVS [%s/%s]: (hex) %s...", namespace, key, hex);
	}
        
        
    } else {
        // Si no se encontró la clave o el tamaño requerido excede el buffer, registrar una advertencia
        ESP_LOGW(TAG, "No se encontro clave o tamanio excede max_len en [%s/%s]", namespace, key);
    }

    // Cerrar el handle para liberar recursos de NVS
    nvs_close(nvs);

    // Devolver el resultado final de la operación (ESP_OK si tuvo éxito, otro código si falló)
    return err;
}

/**
 * Borra una clave específica dentro de un espacio de nombres (namespace) en NVS.
 * Esto es útil para eliminar configuraciones o datos que ya no se necesitan.
 */
esp_err_t nvs_utils_erase_key(const char *namespace, const char *key) {

    // Validación: verificar que los punteros no sean NULL para evitar errores
    if (!namespace || !key) {
        printf("Error: parámetros nulos en erase_key\n");
        return ESP_ERR_INVALID_ARG;  // Retorna error de argumento inválido
    }

    // Validación: verificar que 'namespace' y 'key' no excedan la longitud máxima permitida (15 caracteres)
    if (strlen(namespace) > 15 || strlen(key) > 15) {
        printf("Error: 'namespace' o 'key' exceden los 15 caracteres permitidos por NVS.\n");
        return ESP_ERR_INVALID_ARG;  // Retorna error de argumento inválido
    }

    // Log informativo que indica qué clave y namespace se van a borrar
    ESP_LOGI(TAG, "Borrando clave NVS [%s/%s]", namespace, key);

    // Declaración del handle para abrir el espacio de nombres
    nvs_handle_t nvs;

    // Abrir el espacio de nombres en modo lectura/escritura para permitir borrar
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &nvs);

    // Verificar si la apertura fue exitosa
    if (err != ESP_OK) {
        // Registrar el error si falla la apertura y devolver el código de error
        ESP_LOGE(TAG, "Error abriendo NVS para borrar: %s", esp_err_to_name(err));
        return err;
    }

    // Intentar borrar la clave indicada dentro del espacio de nombres abierto
    err = nvs_erase_key(nvs, key);

    // Si la eliminación fue exitosa
    if (err == ESP_OK) {
        // Confirmar el borrado en la memoria flash
        nvs_commit(nvs);
        ESP_LOGI(TAG, "Clave [%s/%s] borrada correctamente", namespace, key);
    } else {
        // Si hubo algún problema al borrar, registrar una advertencia con el motivo
        ESP_LOGW(TAG, "No se pudo borrar clave [%s/%s]: %s", namespace, key, esp_err_to_name(err));
    }

    // Cerrar el handle para liberar recursos
    nvs_close(nvs);

    // Retornar el código resultado de la operación (éxito o error)
    return err;
}
