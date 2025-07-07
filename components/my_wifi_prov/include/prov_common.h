#pragma once
// Esta línea evita que el archivo se incluya más de una vez durante la compilación, previniendo errores por definiciones repetidas

#include <esp_err.h>
// Se incluye el archivo de ESP-IDF que define los tipos y códigos de error estándar (como ESP_OK o ESP_FAIL).
// Estos códigos permiten saber si las funciones terminaron correctamente o si hubo algún problema.

// Esta función prepara el sistema para el proceso de provisionamiento Wi-Fi.
// Inicializa los recursos necesarios para que el dispositivo pueda recibir los datos de la red Wi-Fi.
// Debe llamarse antes de cualquier otra función de provisionamiento.
// Devuelve un código de error para indicar si la inicialización fue exitosa.
esp_err_t my_wifi_prov_mgr_init(void);

// Esta función inicia el proceso de provisionamiento Wi-Fi.
// Permite que el dispositivo entre en modo de espera para recibir los datos de la red Wi-Fi desde una aplicación móvil.
// Recibe varios parámetros:
//   - security: Especifica el nivel de seguridad que se utilizará durante el proceso de provisionamiento Wi-Fi.
//               Por ejemplo, puede ser un valor que indique si se usará seguridad básica (sin autenticación) o seguridad avanzada (como SRP6a).
//               Este parámetro determina cómo se protegerán los datos que se intercambian entre la app y el dispositivo.
//
//   - sec_params: Es un puntero a una estructura que contiene los parámetros específicos requeridos por el tipo de seguridad seleccionado.
//                 Por ejemplo, si se usa seguridad avanzada, aquí se pasan datos como el "salt" y el "verifier" necesarios para la autenticación segura.
//                 Si se usa seguridad básica, este parámetro puede ser NULL o apuntar a una estructura vacía.
//
//   - service_name: Es una cadena de texto que representa el nombre del servicio de provisionamiento.
//                   Este nombre es visible para el usuario en la aplicación móvil y sirve para identificar el dispositivo entre varios disponibles.
//                   Por ejemplo, puede ser "ReptiTrack_BLE" o cualquier nombre personalizado.
//
//   - service_key: Es una cadena de texto opcional que funciona como una contraseña o clave de acceso adicional.
//                  Si se proporciona, la aplicación móvil deberá ingresar esta clave para poder iniciar el proceso de provisionamiento con el dispositivo.
//                  Si no se requiere una clave adicional, este parámetro puede ser NULL.
//
// Devuelve un código de error (tipo esp_err_t) que indica si el proceso de provisionamiento Wi-Fi se inició correctamente o si ocurrió algún problema.
// Por ejemplo, devuelve ESP_OK si todo salió bien, o un código de error específico si hubo algún
esp_err_t my_wifi_prov_mgr_start(int security, const void *sec_params, const char *service_name, const char *service_key);

// Esta función libera todos los recursos que se usaron durante el proceso de provisionamiento.
// Es importante llamarla cuando el dispositivo ya está conectado a la red y no necesita seguir esperando datos de configuración.
// Así se libera memoria y se evitan posibles problemas por recursos no liberados.
void my_wifi_prov_mgr_deinit(void);

// Esta función imprime en la consola un enlace para escanear un código QR desde la aplicación móvil 
// El código QR contiene información como el nombre del dispositivo, usuario, clave de acceso y el tipo de conexión (por ejemplo, BLE).
// Esto facilita que el usuario conecte la app con el dispositivo sin tener que escribir datos manualmente.
void my_wifi_prov_print_qr(const char *service_name, const char *username, const char *pop, const char *transport);

// Esta es la función principal que controla todo el proceso de provisionamiento Wi-Fi
// Se encarga de iniciar la espera de datos, manejar los eventos y coordinar el flujo completo para conectar el dispositivo a la red
// Se recomienda llamarla desde el archivo principal del programa (app_main.c) para que el dispositivo pueda conectarse a internet de forma sencilla
void my_wifi_prov_startup(void);

// Funcion que imprime cuales son las credenciales de internet guardardas en la memoria no volatil  (Non-Volatile Storage) 
void print_current_wifi_info(void);
	