#include "rtc_time.h"                       // Inclusión del encabezado propio donde  se declaran estas funciones.
#include "esp_log.h"                        // Para utilizar funciones de logging (ESP_LOGI, ESP_LOGE, etc.).
#include "esp_http_client.h"               // Para hacer peticiones HTTP.
#include "esp_system.h"                    // Funciones básicas del sistema ESP32.
#include "cJSON.h"                         // Biblioteca para parsear JSON.
#include <stdlib.h>
#include <string.h>                        // Para funciones de manejo de cadenas como memcpy, strlen, etc.
#include <time.h>                          // Para estructuras y funciones relacionadas con el tiempo.
#include "nvs_utils.h"    					// Incluye utilidades personalizadas para simplificar el uso de NVS.
#include "nvs.h"         					 // Incluye las definiciones estándar de NVS, como errores como ESP_ERR_NVS_NOT_FOUND.



static const char *TAG = "rtc_time";       // Etiqueta para logs relacionados con este módulo.

// Define el namespace (espacio de claves) en NVS donde se guarda la información del RTC
static const char *NVS_NAMESPACE = "rtc";

// Define la clave específica dentro del namespace para el estado de sincronización
static const char *NVS_KEY_SYNCED = "synced";

 
 
void rtc_time_init_timezone(void) {
    setenv("TZ", "UTC0", 1);
    tzset();
}

 
 
/**
 * @brief Consulta si el RTC ha sido sincronizado previamente.
 *
 * Esta función revisa en la memoria NVS si existe una bandera persistente que indique
 * si el RTC ya fue sincronizado con un servidor de hora. Es útil para decidir
 * si se necesita volver a sincronizar al reiniciar el dispositivo.
 *
 * @param[out] out_synced Puntero a una variable booleana donde se almacenará el resultado.
 *                        true si fue sincronizado previamente, false en caso contrario.
 *
 * @return ESP_OK si la operación fue exitosa (incluyendo el caso en que no se encontró la clave).
 *         ESP_ERR_INVALID_ARG si el puntero proporcionado es nulo.
 *         Otro código de error si ocurrió un fallo inesperado al leer desde NVS.
 */
esp_err_t rtc_time_is_synchronized(bool *out_synced) {
    // Verifica que el puntero proporcionado no sea nulo para evitar un acceso inválido a memoria.
    if (!out_synced) return ESP_ERR_INVALID_ARG;

    // Variable temporal para almacenar el valor entero que representa el estado (0 = no sincronizado, 1 = sincronizado).
    int stored_value = 0;

    // Tamaño de la variable que esperamos leer desde NVS.
    size_t size = sizeof(stored_value);

    // Intenta leer desde NVS el valor asociado a la clave "synced" dentro del namespace "rtc".
    // El valor es interpretado como un blob (almacenamiento genérico de datos).
    esp_err_t err = nvs_utils_load_blob(NVS_NAMESPACE, NVS_KEY_SYNCED, &stored_value, size, &size);

    // Si la lectura fue exitosa...
    if (err == ESP_OK) {
        // ... convertimos el valor leido a booleano (cualquier valor distinto de 0 es true).
        *out_synced = (stored_value != 0);
        //ESP_LOGI(TAG, "Flag sincronizacion leido: %d", stored_value);

    // Si la clave no fue encontrada en NVS, lo interpretamos como "no sincronizado" (estado inicial).
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out_synced = false;

        // No lo consideramos un error porque es un caso válido en el primer arranque.
        err = ESP_OK;

        ESP_LOGI(TAG, "Flag sincronizacion no encontrado");

    // Si ocurrió cualquier otro error al leer desde NVS, lo reportamos.
    } else {
        ESP_LOGE(TAG, "Error leyendo flag sincronizacion: %s", esp_err_to_name(err));
    }

    // Retornamos el resultado final (ya sea éxito o error inesperado).
    return err;
}


/**
 * @brief Marca el RTC como sincronizado, guardando el estado en NVS.
 * 
 * Esta función guarda el valor booleano de sincronización (`true` o `false`)
 * de forma persistente en NVS, bajo el namespace `"rtc"` y clave `"synced"`.
 * Esto permite que el estado sobreviva reinicios del dispositivo.
 * 
 * @param[in] synced Booleano que representa si el RTC ya está sincronizado (true) o no (false).
 * 
 * @return ESP_OK si el valor fue guardado correctamente,
 *         o un código de error si falló la escritura en NVS.
 */
esp_err_t rtc_time_set_synchronized(bool synced) {
    // Convierte el booleano 'synced' a un entero (1 si es true, 0 si es false).
    // Esto se hace porque NVS no guarda booleanos directamente, pero sí enteros.
    int val = synced ? 1 : 0;

    // Intenta guardar el valor 'val' en NVS bajo la clave "synced" en el namespace "rtc".
    // Usa una función utilitaria que internamente maneja apertura, escritura y cierre de NVS.
    esp_err_t err = nvs_utils_save_blob(NVS_NAMESPACE, NVS_KEY_SYNCED, &val, sizeof(val));

    // Si se guardó correctamente, lo confirmamos con un mensaje en el log.
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Flag sincronizacion guardado: %d", val);
    } else {
        // Si hubo error al guardar, lo registramos en el log con el nombre del error.
        ESP_LOGE(TAG, "Error guardando flag sincronizacion: %s", esp_err_to_name(err));
    }

    // Devolvemos el resultado de la operación, ya sea éxito o error.
    return err;
}

/**
 * @brief Elimina de NVS la bandera que indica si el RTC fue sincronizado.
 *
 * Esta función borra la clave `"synced"` del namespace `"rtc"` en NVS. 
 * Se usa típicamente para restablecer el estado de sincronización, por ejemplo, al hacer un reset manual.
 *
 * Si la clave no existe, no se considera un error grave.
 *
 * @return ESP_OK si la clave fue borrada correctamente o si no existía.
 *         Otro código de error si ocurrió un problema al acceder a NVS.
 */
esp_err_t rtc_time_clear_synchronized(void) {
    // Intenta borrar la clave "synced" dentro del namespace "rtc" en la memoria NVS.
    esp_err_t err = nvs_utils_erase_key(NVS_NAMESPACE, NVS_KEY_SYNCED);

    // Si la operación fue exitosa, lo informamos en el log.
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Flag sincronizacion borrado correctamente");

    // Si la clave no existía, lo notificamos como advertencia, pero no se trata como error.
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Flag sincronizacion no encontrado al borrar");
        
        // Ajustamos el código de retorno a ESP_OK porque la operación no es crítica.
        err = ESP_OK;

    // Si ocurrió otro tipo de error (por ejemplo, corrupción de NVS, falta de espacio, etc.),
    // lo registramos como error crítico.
    } else {
        ESP_LOGE(TAG, "Error borrando flag sincronización: %s", esp_err_to_name(err));
    }

    // Devolvemos el resultado final de la operación.
    return err;
}



/**	
 * @brief Parsea la respuesta JSON de WorldTimeAPI y extrae la fecha/hora.
 *
 * Esta función toma el JSON devuelto por la API WorldTimeAPI, extrae el campo "datetime",
 * y lo convierte a una estructura `struct tm` que representa la fecha/hora local (sin zona horaria).
 *
 * @param[in]  json_str Cadena JSON completa recibida desde la API.
 * @param[out] out_tm   Puntero a una estructura tm donde se almacenará la fecha/hora.
 *
 * @return ESP_OK si fue exitoso, o un código de error (ESP_ERR_INVALID_ARG, ESP_FAIL, etc).
 */
esp_err_t parse_worldtimeapi_response(const char *json, struct tm *out_tm)
{
    const char *TAG = "rtc_time";
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "No se pudo parsear el JSON");
        return ESP_FAIL;
    }

    // Usar el campo "utc_datetime"
    const cJSON *datetime = cJSON_GetObjectItem(root, "utc_datetime");
    if (!datetime || !cJSON_IsString(datetime)) {
        ESP_LOGE(TAG, "Campo 'utc_datetime' no encontrado o no es string");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    const char *dt_str = datetime->valuestring;

    // Espera formato: "YYYY-MM-DDTHH:MM:SS"
    if (strlen(dt_str) < 19) {
        ESP_LOGE(TAG, "Formato utc_datetime inesperado");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    memset(out_tm, 0, sizeof(struct tm));
    int parsed = sscanf(dt_str, "%4d-%2d-%2dT%2d:%2d:%2d",
                        &out_tm->tm_year, &out_tm->tm_mon, &out_tm->tm_mday,
                        &out_tm->tm_hour, &out_tm->tm_min, &out_tm->tm_sec);

    if (parsed != 6) {
        ESP_LOGE(TAG, "No se pudo extraer la fecha/hora de utc_datetime");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    out_tm->tm_year -= 1900;
    out_tm->tm_mon -= 1;

    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief Sincroniza la hora del ESP32 usando un servidor en línea (WorldTimeAPI).
 *
 * Esta función realiza una petición HTTP GET a WorldTimeAPI usando la zona horaria proporcionada.
 * Si la respuesta es válida, extrae la hora, actualiza el RTC del sistema y guarda un flag en NVS
 * indicando que el reloj ya fue sincronizado.
 *
 * @param[in] timezone Cadena de texto con la zona horaria, por ejemplo: "America/Mexico_City".
 * @return ESP_OK si la sincronización fue exitosa. Otro código de error si algo falla.
 */
 
esp_err_t rtc_time_sync_with_timezone(const char *timezone) {
    // Verifica que el puntero no sea nulo y que la cadena no esté vacía
    if (!timezone || strlen(timezone) == 0) {
        return ESP_ERR_INVALID_ARG; // Devuelve error por argumento inválido
    }

    // Declaramos un buffer donde se construirá la URL de la solicitud HTTP
    char url[128];
    // Usamos snprintf para escribir la URL en el buffer, incluyendo la zona horaria
    int len = snprintf(url, sizeof(url), "http://worldtimeapi.org/api/timezone/%s", timezone);
    
    // Validamos si hubo error al construir la URL o si excede el tamaño del buffer
    if (len < 0 || len >= sizeof(url)) {
        ESP_LOGE(TAG, "Zona horaria invalida");  // Error de formato o overflow
        return ESP_ERR_INVALID_ARG;
    }

    // Configuramos el cliente HTTP: método GET, URL y timeout de 5 segundos
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };

    // Inicializa el cliente HTTP con la configuración anterior
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "No se pudo inicializar el cliente HTTP");
        return ESP_FAIL; // Fallo general al crear cliente
    }

    // Ejecuta la solicitud HTTP GET
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error en solicitud HTTP: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client); // Limpia recursos del cliente
        return err;
    }

    // Obtiene el código de estado HTTP (ej. 200 OK)
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "Codigo de respuesta HTTP inesperado: %d", status);
        esp_http_client_cleanup(client); // Cierra el cliente aunque sea error
        return ESP_FAIL;
    }

   // Abre la conexión (requerido para poder leer la respuesta completa)
	err = esp_http_client_open(client, 0);
	if (err != ESP_OK) {
	    ESP_LOGE(TAG, "Error abriendo conexión HTTP: %s", esp_err_to_name(err));
	    esp_http_client_cleanup(client);
	    return err;
	}
	
	// (Opcional) Lee los headers si necesitas verificar algo adicional
	int headers_len = esp_http_client_fetch_headers(client);
	if (headers_len < 0) {
	    ESP_LOGW(TAG, "No se pudieron leer headers");
	}
	
	// Reservamos un buffer de lectura (WorldTimeAPI responde < 2KB)
	char *buffer = malloc(2048);
	if (!buffer) {
	    ESP_LOGE(TAG, "Sin memoria para buffer");
	    esp_http_client_cleanup(client);
	    return ESP_ERR_NO_MEM;
	}
	
	// Lee el cuerpo completo de la respuesta
	int read_len = esp_http_client_read_response(client, buffer, 2048);
	esp_http_client_cleanup(client); // Libera el cliente después de leer
	
	if (read_len <= 0) {
	    ESP_LOGE(TAG, "Error leyendo cuerpo HTTP");
	    free(buffer);
	    return ESP_FAIL;
	}
	
	buffer[read_len] = '\0'; // Asegura que sea una cadena válida

    // Estructura donde se guardará la hora parseada
    struct tm timeinfo;
    // Llama a la función que extrae los datos de la respuesta JSON
    err = parse_worldtimeapi_response(buffer, &timeinfo);
    free(buffer); // Libera el buffer después de parsear

    // Si hubo error al interpretar la respuesta JSON
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error parseando la hora");
        return err;
    }

    // Convierte struct tm (año, mes, hora, etc) a timestamp UNIX (segundos desde 1970)
    time_t t = mktime(&timeinfo);
    if (t == -1) {
        ESP_LOGE(TAG, "Error convirtiendo struct tm a time_t");
        return ESP_FAIL;
    }

    // Crea estructura timeval con segundos y microsegundos
    struct timeval now = { .tv_sec = t, .tv_usec = 0 };
    // Actualiza el reloj del sistema (RTC interno del ESP32)
    settimeofday(&now, NULL);

    // Guarda en NVS una bandera indicando que el RTC fue sincronizado correctamente
    rtc_time_set_synchronized(true);

    // Muestra en el log la hora sincronizada en formato legible
    ESP_LOGI(TAG, "Hora sincronizada: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
             

    // Devuelve ESP_OK para indicar que todo fue exitoso
    return ESP_OK;
}

/**
 * @brief Imprime la hora actual si el sistema ha sido sincronizado previamente.
 *
 * Verifica en NVS si el RTC fue sincronizado antes. Si fue así, imprime la hora actual.
 *
 * @return ESP_OK si se imprimió correctamente, ESP_ERR_INVALID_STATE si no está sincronizado.
 */
esp_err_t rtc_time_print_current(void) {
    // Variable para guardar si el sistema está sincronizado o no.
    bool synced = false;

    // Consulta si el RTC fue sincronizado previamente leyendo desde NVS.
    esp_err_t err = rtc_time_is_synchronized(&synced);

    // Si ocurrió un error al consultar, o la sincronización no se ha hecho, se retorna error.
    if (err != ESP_OK || !synced) {
        ESP_LOGW(TAG, "RTC no sincronizado aun");
        return ESP_ERR_INVALID_STATE;
    }

    // Obtiene la hora actual del sistema como time_t (segundos desde Epoch 1970).
    time_t now;
    time(&now);  // Equivalente a: now = time(NULL);

    // Convierte la hora actual a una estructura `struct tm` con los componentes separados (año, mes, día, etc).
	struct tm timeinfo;
	gmtime_r(&now, &timeinfo);

    // Imprime la hora actual en formato legible: "YYYY-MM-DD HH:MM:SS"
    ESP_LOGI(TAG, "%04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900,  // El año en `tm` es desde 1900.
             timeinfo.tm_mon + 1,      // El mes es base 0 (enero = 0).
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min,
             timeinfo.tm_sec);

    // Retorna éxito.
    return ESP_OK;
}


/**
 * @brief Obtiene la hora actual del sistema si el RTC ha sido sincronizado previamente.
 *
 * Esta función retorna la hora actual del sistema (RTC) en formato `time_t`, que representa
 * los segundos transcurridos desde el 1 de enero de 1970 (Epoch). Antes de devolver la hora,
 * valida que el RTC haya sido sincronizado previamente mediante `rtc_time_sync_with_timezone()`.
 *
 * @param[out] out_time Puntero a una variable `time_t` donde se almacenará el valor de la hora actual.
 *                      Debe ser un puntero válido (no nulo).
 *
 * @return ESP_OK si se obtuvo la hora correctamente.
 *         ESP_ERR_INVALID_ARG si el puntero proporcionado es nulo.
 *         ESP_ERR_INVALID_STATE si el RTC aún no ha sido sincronizado.
 */
esp_err_t rtc_time_get_current(time_t *out_time) {
    // Verifica que el puntero de salida no sea nulo.
    if (!out_time) return ESP_ERR_INVALID_ARG;

    // Variable booleana para saber si el sistema está sincronizado.
    bool synced = false;

    // Consulta a NVS si el RTC fue sincronizado previamente.
    esp_err_t err = rtc_time_is_synchronized(&synced);

    // Si hubo error o el RTC no ha sido sincronizado, se retorna estado inválido.
    if (err != ESP_OK || !synced) {
        return ESP_ERR_INVALID_STATE;
    }

    // Variable temporal para almacenar la hora actual en segundos desde Epoch.
    time_t now;
    time(&now);  // Asigna a 'now' el tiempo actual del sistema (equivalente a now = time(NULL)).

    // Escribe el resultado en el puntero proporcionado por el usuario.
    *out_time = now;

    // Retorna éxito.
    return ESP_OK;
}


time_t timegm(struct tm *tm) {
    char *tz = getenv("TZ");
    setenv("TZ", "", 1); // UTC
    tzset();
    time_t ret = mktime(tm);
    if (tz)
        setenv("TZ", tz, 1);
    else
        unsetenv("TZ");
    tzset();
    return ret;
}

/**
 * @brief Convierte una cadena de fecha y hora en formato ISO 8601 a una estructura `struct tm` en UTC.
 *
 * Esta función recibe una cadena con una fecha/hora en formato ISO 8601 (ejemplo: "2025-07-26T14:15:30Z"),
 * que representa la fecha y hora en Tiempo Universal Coordinado (UTC). Extrae la parte relevante,
 * la convierte a una estructura `struct tm` en UTC.
 *
 * @param iso_str Cadena con la fecha/hora en formato ISO 8601. Debe contener al menos
 *                "YYYY-MM-DDTHH:MM:SS" y terminar con 'Z' o incluir zona UTC.
 * @param out_tm Puntero a variable `struct tm` donde se almacenará el resultado en UTC.
 *
 * @return ESP_OK si la conversión fue exitosa,
 *         ESP_ERR_INVALID_ARG si los argumentos son inválidos o el formato es incorrecto,
 *         ESP_FAIL si la conversión a `time_t` falló.
 */
esp_err_t rtc_time_parse_iso8601_to_tm(const char *iso_str, struct tm *out_tm) {
    if (!iso_str || !out_tm) return ESP_ERR_INVALID_ARG;

    size_t iso_len = strlen(iso_str);
    if (iso_len < 19) return ESP_ERR_INVALID_ARG; // mínimo "YYYY-MM-DDTHH:MM:SS"

    // Copiar solo la parte "YYYY-MM-DDTHH:MM:SS"
    char buf[20];
    memcpy(buf, iso_str, 19);
    buf[19] = '\0';

    // Cambiar 'T' por espacio para strptime
    buf[10] = ' ';

    struct tm tm_val = {0};
    if (!strptime(buf, "%Y-%m-%d %H:%M:%S", &tm_val)) {
        return ESP_ERR_INVALID_ARG;
    }

    tm_val.tm_isdst = 0; // ignorar horario de verano en UTC

    // Convertir struct tm UTC a time_t (segundos desde epoch)
    time_t t = timegm(&tm_val); // Usa timegm estándar
    if (t == (time_t)-1) return ESP_FAIL;

    // Copiar resultado UTC al out_tm usando gmtime_r
    if (gmtime_r(&t, out_tm) == NULL) return ESP_FAIL;

    return ESP_OK;
}

/**
 * @brief Devuelve la hora actual del RTC formateada como una cadena legible.
 * 
 * Esta función obtiene la hora actual (asumiendo que ya fue sincronizada previamente)
 * y la convierte a una cadena con el siguiente formato:
 *     "YYYY-MM-DD HH:MM:SS"
 * 
 * Este formato es ampliamente utilizado para mostrar fechas de forma legible en logs,
 * interfaces, reportes o estructuras JSON. No incluye zona horaria ni separador 'T'.
 * 
 * Ejemplo de salida:
 *     2025-07-31 21:03:00
 * 
 * @param[out] out_str  Buffer donde se escribirá la cadena resultante.
 *                      Debe tener espacio suficiente (recomendado: al menos 32 bytes).
 * @param[in]  max_len  Longitud máxima del buffer proporcionado.
 *                      Si es menor a 20, se considera inválido.
 * 
 * @return ESP_OK si se obtuvo y formateó correctamente la fecha/hora.
 *         ESP_ERR_INVALID_ARG si los argumentos son nulos o el buffer es muy pequeño.
 *         ESP_ERR_INVALID_STATE si el RTC aún no ha sido sincronizado.
 *         ESP_FAIL si ocurrió un error al formatear la fecha.
 */
esp_err_t rtc_time_get_formatted_readable(char *out_str, size_t max_len)
{
    // Validamos los argumentos de entrada
    if (!out_str || max_len < 20) {
        return ESP_ERR_INVALID_ARG;
    }

    // Obtenemos la hora actual del sistema (en formato time_t)
    time_t now;
    esp_err_t err = rtc_time_get_current(&now);
    if (err != ESP_OK) {
        return err;  // Retornamos el error si el RTC no está sincronizado
    }

    // Convertimos time_t a estructura tm (desglosada en año, mes, día, etc.)
	struct tm timeinfo;
	gmtime_r(&now, &timeinfo);

    // Formateamos la fecha y hora en el buffer de salida
    // Formato: "YYYY-MM-DD HH:MM:SS" (ej: 2025-07-31 21:03:00)
    size_t written = strftime(out_str, max_len, "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    // Verificamos que strftime haya escrito correctamente
    if (written == 0) {
        return ESP_FAIL;  // El buffer fue insuficiente o hubo un error
    }

    // Todo salió bien
    return ESP_OK;
}

