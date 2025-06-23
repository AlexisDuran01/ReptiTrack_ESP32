/*
 * Este archivo es el punto de inicio del programa.
 * Aquí es donde comienza a funcionar el dispositivo cuando se enciende.
 * Su función principal es arrancar el proceso para conectar el dispositivo a la red Wi-Fi
 * y mantenerlo funcionando todo el tiempo.
 */

#include "prov_common.h" // Incluye el archivo que tiene la función para iniciar la conexión Wi-Fi
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Esta es la función principal que se ejecuta al encender el esp32
void app_main(void)
{
    // Aquí se llama a la función que inicia el proceso de conexión Wi-Fi.
    // Esta función se encarga de todo lo necesario para que el dispositivo pueda conectarse a internet.
    my_wifi_prov_startup();

    // Este es un ciclo que nunca termina.
    // Sirve para que el dispositivo siga funcionando y no se apague.
    // Dentro del ciclo, el dispositivo simplemente espera un segundo y vuelve a esperar, una y otra vez.
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Espera 1 segundo antes de repetir el ciclo
    }
}