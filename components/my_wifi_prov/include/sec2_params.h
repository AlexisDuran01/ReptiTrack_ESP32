#pragma once
// Esta directiva asegura que el archivo solo se incluya una vez durante la compilación, evitando redefiniciones.

//
// Declaraciones externas de los parámetros de seguridad para el esquema Security 2 (SRP6a) usado en el proceso de provisionamiento Wi-Fi.
// Estos parámetros son requeridos por el protocolo de seguridad para autenticar y establecer una conexión segura entre la app y el dispositivo.
//

extern const char sec2_salt[];
// sec2_salt: Array que contiene el valor de "salt" utilizado en el protocolo SRP6a para la autenticación segura.
// El "salt" es un valor aleatorio generado durante la configuración inicial del dispositivo y se utiliza para proteger la contraseña del usuario contra ataques de diccionario y rainbow tables.
// Durante el proceso de autenticación, el salt se combina con la contraseña para generar el "verifier" y también se envía a la app para que ambos lados puedan calcular los valores criptográficos necesarios.
// Salt: sal (valor aleatorio para fortalecer la seguridad de la contraseña)
// El valor real se define en el archivo sec2_params.c.

extern const int sec2_salt_len;
// sec2_salt_len: Longitud del array sec2_salt. Permite saber cuántos bytes tiene el salt.

extern const char sec2_verifier[];
// sec2_verifier: Array que contiene el "verifier" generado para este dispositivo, usado en el intercambio seguro de claves durante el provisioning.
// El "verifier" es un valor criptográfico calculado a partir de la contraseña del usuario y el salt, siguiendo el algoritmo SRP6a (Secure Remote Password).
// El verifier permite que el dispositivo autentique a la app sin necesidad de almacenar ni transmitir la contraseña real, aumentando la seguridad del proceso de provisionamiento.
// Verifier: verificador (valor que permite autenticar sin exponer la contraseña)
// El valor real se define en el archivo sec2_params.c.

extern const int sec2_verifier_len;
// sec2_verifier_len: Longitud del array sec2_verifier. Permite saber cuántos bytes tiene