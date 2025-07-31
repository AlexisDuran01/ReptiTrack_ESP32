	#ifndef SENSOR_H         // Previene que este archivo se incluya más de una vez
	#define SENSOR_H         //     en un mismo archivo de compilación
	
	#include <stdbool.h>     // Incluye el tipo bool (true/false)
	
	// Declaramos que existe una estructura llamada 'Sensor'
	// Esta es una "forward declaration", o declaración adelantada:
	// Permite usar el tipo 'Sensor' antes de haberlo definido completamente.
	// Esto es útil para funciones que lo reciben como parámetro.
	typedef struct Sensor Sensor;
	
	// Este es un puntero a función, llamado 'sensor_init_t'.
	// Sintaxis de typedef para punteros a función:
	// typedef <tipo_de_retorno> (*<nombre_nuevo_tipo>)(<argumentos>);
	typedef bool (*sensor_init_t)(Sensor* self);
	// Aquí:
	// - `bool` es el tipo de retorno (true si la inicialización fue exitosa, false si falló)
	// - `(*sensor_init_t)` define un nombre para un nuevo tipo de puntero a función
	// - `(Sensor* self)` significa que la función recibirá un puntero a Sensor

	//  ¿Qué es `Sensor* self`?	
	// En C no hay objetos, pero puedes simularlos
	// Se suele llamar `self` (como en Python) al puntero que representa la instancia actual del objeto (en este caso, el sensor).
	// Así puedes acceder dentro de la función a sus campos: self->name, self->internal_data, etc.
	
	
	// Similar al anterior, pero esta función devuelve un puntero genérico (`void*`)
	typedef void* (*sensor_read_t)(Sensor* self);
	// - `void*` es un puntero que puede apuntar a cualquier tipo de estructura.
	// - Esto permite devolver distintos tipos de datos dependiendo del sensor (temperatura, luz, distancia, etc.).
	// - El usuario que llama a esta función debe saber qué tipo de datos devuelve.
	// 🔸 Por ejemplo: un DHT11 podría devolver un `DHTData*`, que es una estructura con temperatura y humedad.
	
		
	// Esta función también recibe un `Sensor* self`
	// y devuelve un `bool` indicando si logró publicar los datos (por MQTT u otro medio)
	typedef bool (*sensor_publish_t)(Sensor* self);
	// Esta función es opcional: puedes poner NULL si no necesitas publicar
	
// Definimos la estructura Sensor

struct Sensor {
    const char* name;
    // - `const char*` es un puntero a una cadena de caracteres constante
    // - Sirve para guardar el nombre del sensor (por ejemplo: "Sensor DHT11 #1")

    sensor_init_t init;
    // - Este campo es un puntero a una función de tipo sensor_init_t
    // - Significa que puedes hacer: `sensor->init(sensor);`

    sensor_read_t read;
    // - Puntero a función para leer datos del sensor
    // - Devuelve un puntero a los datos leídos

    sensor_publish_t publish;
    // - Puntero a función para publicar datos (opcional)

    void* internal_data;
    // - Este es un puntero genérico a datos internos específicos del sensor
    // - Cada tipo de sensor puede usar su propia estructura y asignarla aquí
    //   Por ejemplo, un DHT11 podría definir:
    //     struct DHTInternal {
    //         int gpio_num;
    //         float last_temp;
    //         float last_humidity;
    //     };
    //     ...y luego guardar un puntero a esa estructura en `internal_data`.
};

	#endif // SENSOR_H         //  Cierra el "include guard" para prevenir dobles inclusiones
