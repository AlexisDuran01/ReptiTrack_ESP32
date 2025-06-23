#pragma once

/*
 * Este es un archivo de encabezado (.h) en C.
 * Los archivos .h se usan para declarar configuraciones, funciones o variables que serán utilizadas en otros archivos del proyecto.
 * En este caso, este archivo sirve para definir qué pines del ESP32 se usan para el LED y el botón de reprovisionamiento Wi-Fi.
 * Si en algún momento se quiere cambiar el pin del LED o del botón, solo necesitas modificar los valores aquí y el cambio se aplicará en todo el proyecto
 * Esto ayuda a mantener el código organizado y fácil de modificar
 */

// Número de pin (GPIO) donde está conectado el LED que indica el estado de la provisión
#define PROV_LED_GPIO        2   

// Número de pin (GPIO) donde está conectado el botón físico para iniciar el receteo del aprovisinamiento
#define PROV_BUTTON_GPIO     16  