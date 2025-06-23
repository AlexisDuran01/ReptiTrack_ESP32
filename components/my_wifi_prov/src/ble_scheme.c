#include "esp_log.h"                         // Permite mostrar mensajes en la consola para depuración
#include "esp_err.h"                         // Maneja los códigos de error de ESP-IDF
#include <wifi_provisioning/manager.h>       // Permite gestionar el proceso de provisionamiento Wi-Fi
#include <wifi_provisioning/scheme_ble.h>    // Permite usar BLE (Bluetooth Low Energy) como método de provisionamiento
#include <wifi_provisioning/scheme_softap.h> // Permite usar SoftAP como método alternativo (no usado aquí, pero incluido por compatibilidad)

/*
 * Este archivo contiene funciones auxiliares para configurar y personalizar el proceso de provisionamiento Wi-Fi usando BLE (Bluetooth Low Energy) en el ESP32.
 *
 * ¿Qué es el esquema BLE?
 * El "esquema BLE" es una forma de permitir que el dispositivo ESP32 reciba la configuración de red Wi-Fi de manera inalámbrica usando Bluetooth Low Energy.
 * Gracias a este esquema, el usuario puede conectar el dispositivo a la corriente, abrir una app en su celular y enviarle los datos de la red Wi-Fi sin cables ni conexiones físicas.
 * BLE es ideal para este proceso porque es de bajo consumo, seguro y ampliamente soportado por teléfonos móviles modernos.
 *
 * ¿Para qué sirve este archivo?
 * Su objetivo principal es facilitar la inicialización y personalización del esquema BLE, permitiendo que el dispositivo pueda ser configurado de manera inalámbrica desde una aplicación móvil.
 * Aquí se encuentran funciones para:
 *   - Inicializar el esquema BLE para el proceso de provisionamiento.
 *   - Establecer un UUID de servicio BLE personalizado, lo que ayuda a identificar el dispositivo entre otros dispositivos BLE cercanos.
 *   - Configurar datos de fabricante personalizados, que pueden ser usados para mostrar información específica de tu producto durante el escaneo BLE.
 *   - Asignar una dirección Bluetooth aleatoria al dispositivo, lo que puede mejorar la privacidad o evitar conflictos de direcciones.
 * 
 * Este archivo permite adaptar y controlar cómo se presenta y comporta el dispositivo durante el proceso de configuración Wi-Fi por BLE, haciendo más sencillo y seguro el emparejamiento con la app móvil.
 
 * Sobre los comentarios de documentación:
 *   - @brief: Da una breve descripción de lo que hace la función.
 *   - @param: Explica para qué sirve cada parámetro que recibe la función.
 *   - @return: Describe qué valor devuelve la función y qué significa ese valor.
 * Estas etiquetas ayudan a otros programadores (y a herramientas automáticas) a entender rápidamente el propósito y uso de cada función, y permiten generar documentación automática del código.
 
 */

static const char *TAG = "ble_scheme"; // Etiqueta para identificar los mensajes de este archivo en la consola

/**
 * @brief Inicializa el esquema BLE para la provisión WiFi.
 * Esta función prepara el sistema para usar BLE como método de provisionamiento.
 * Si se requiere alguna configuración especial antes de iniciar el provisioning BLE, se puede agregar aquí.
 * Actualmente solo muestra un mensaje y devuelve ESP_OK.
 */
esp_err_t ble_scheme_init(void)
{
    ESP_LOGI(TAG, "Inicializando esquema BLE para provisión WiFi");
    // Inicialización personalizada si es necesario
    return ESP_OK;
}

/**
 * @brief Establece un UUID de servicio BLE personalizado.
 * Permite definir un identificador único para el servicio BLE que usará el dispositivo durante el proceso de provisionamiento.
 * Esto puede ser útil para diferenciar tu producto de otros dispositivos BLE cercanos.
 * 
 * @param uuid128 Puntero a un arreglo de 16 bytes que representa el UUID personalizado.
 * @return esp_err_t Código de error que indica si la operación fue exitosa.
 */
esp_err_t ble_scheme_set_service_uuid(uint8_t *uuid128)
{
    ESP_LOGI(TAG, "Configurando UUID de servicio BLE personalizado");
    return wifi_prov_scheme_ble_set_service_uuid(uuid128);
}

/**
 * @brief Establece datos de fabricante personalizados en la respuesta de escaneo BLE.
 * Permite agregar información específica de tu marca o producto en la respuesta que envía el dispositivo cuando otros equipos lo buscan por BLE.
 * Esto puede ayudar a identificar el dispositivo en aplicaciones móviles o herramientas de escaneo BLE.
 * 
 * @param mfg_data Puntero a los datos de fabricante.
 * @param mfg_data_len Longitud de los datos de fabricante.
 * @return esp_err_t Código de error que indica si la operación fue exitosa.
 */
esp_err_t ble_scheme_set_mfg_data(uint8_t *mfg_data, ssize_t mfg_data_len)
{
    ESP_LOGI(TAG, "Configurando manufacturer data para BLE scan response");
    return wifi_prov_scheme_ble_set_mfg_data(mfg_data, mfg_data_len);
}

/**
 * @brief Establece una dirección aleatoria de Bluetooth para el esquema BLE.
 * Permite asignar una dirección MAC aleatoria al dispositivo para el proceso de provisionamiento BLE.
 * Esto puede ser útil para mejorar la privacidad o evitar conflictos con otras direcciones.
 * 
 * @param rand_addr Puntero a un arreglo de 6 bytes que representa la dirección aleatoria.
 * @return esp_err_t Código de error que indica si la operación fue exitosa.
 */
esp_err_t ble_scheme_set_random_addr(uint8_t *rand_addr)
{
    ESP_LOGI(TAG, "Configurando dirección aleatoria BLE");
    return wifi_prov_scheme_ble_set_random_addr(rand_addr);
}


/*
 * NOTA IMPORTANTE SOBRE EL USO DE ESTAS FUNCIONES:
 *
 * Actualmente, las funciones definidas en este archivo (ble_scheme.c) NO se están utilizando en el flujo principal de tu aplicación.
 * Es decir, en el código actual, no hay ninguna llamada explícita a funciones como:
 *   - ble_scheme_init()
 *   - ble_scheme_set_service_uuid()
 *   - ble_scheme_set_mfg_data()
 *   - ble_scheme_set_random_addr()
 * 
 * Esto significa que, aunque estas funciones están implementadas y documentadas aquí, no afectan el comportamiento de tu dispositivo a menos que las llames desde otro archivo,
 * por ejemplo, desde components/my_wifi_prov/src/app_prov.c o desde la función principal de inicialización de provisioning (como my_wifi_prov_startup).
 *
 * ¿Por qué es importante dejarlas en el proyecto?
 * - Estas funciones permiten personalizar y adaptar el comportamiento del esquema BLE para el provisionamiento Wi-Fi.
 * - Si en el futuro se busca que el dispositivo tenga un UUID BLE único, datos de fabricante personalizados, o una dirección Bluetooth aleatoria para mayor privacidad,
 *   solo necesitas llamar a estas funciones antes de iniciar el proceso de provisionamiento BLE.
 * - Mantenerlas en el proyecto facilita la expansión y personalización del producto sin tener que reescribir o buscar estas utilidades más adelante.
 * - También sirven como referencia y documentación para otros desarrolladores que quieran entender o modificar el proceso de provisionamiento BLE.
 * */