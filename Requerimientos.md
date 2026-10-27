# Proyecto: Laboratorio de concurrencia y memoria

## Descripción del proyecto

Desarrolla una aplicación con interfaz TUI o GUI que permita configurar y ejecutar experimentos controlados de concurrencia y uso de memoria. La aplicación debe iniciar una carga de prueba propia, permitir cambiar algunos de sus parámetros y mostrar cómo responde el sistema operativo.

El propósito es experimentar con hilos, sincronización, memoria virtual, RAM y SWAP mediante pruebas configurables y ejecutadas bajo límites seguros.

La plataforma principal será Linux.

## Requisitos

- Debe contar con una interfaz TUI o GUI para configurar, iniciar, detener y repetir las pruebas.
- Debe permitir cambiar la cantidad de hilos de trabajo e indicar qué tipo utiliza: hilos nativos del sistema operativo o tareas administradas por un runtime.
- Debe ejecutar una carga que reserve memoria virtual y acceda a sus páginas, para que el sistema operativo tenga que gestionarlas y se pueda observar la memoria residente.
- Debe permitir configurar y aplicar límites de memoria y de uso de CPU a la carga de prueba en Linux. El límite de CPU controla el tiempo de procesador disponible para la carga; no cambia la cantidad de núcleos del equipo. Si el entorno no permite aplicar alguno de los límites, la aplicación debe indicarlo claramente y no presentar la prueba como limitada.
- Debe mostrar información de la ejecución, como memoria residente, memoria virtual, fallos de página, SWAP —si está disponible—, duración y resultado de la prueba.
- Debe permitir repetir pruebas con diferentes configuraciones y comparar los resultados.
- La aplicación debe presentar claramente las mediciones obtenidas y distinguir memoria virtual, RAM residente, SWAP y fallos de página.
- La carga debe ejecutarse de manera controlada, poder detenerse y no estar diseñada para agotar la memoria del equipo.
- La aplicación debe poder ejecutarse correctamente para su revisión.

## Pruebas requeridas

- **Concurrencia:** comparar la ejecución de una misma carga con distintas cantidades de hilos.
- **Límite de CPU:** ejecutar una carga bajo distintas configuraciones del límite de CPU y comparar los resultados.
- **Presión de memoria:** variar la cantidad de memoria utilizada bajo un límite configurado y observar las mediciones disponibles, incluyendo SWAP si está habilitado.
- **Acceso compartido y sincronización:** probar varios hilos que accedan a un dato compartido, comparar el comportamiento con y sin sincronización y observar las diferencias.
