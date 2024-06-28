# FreeRTOS Project
Sistemas Operativos II - TP4 - 2024

---

## Objetivo
El objetivo del presente trabajo practico es que el estudiante sea capaz de diseñar, crear, comprobar y validar una aplicacion de tiempo real sobre un RTOS.

Se pide que, utilizando **QEMU**, emulando un sistema **Stellaris LM3S811**, se desarrolle una aplicacion basada en **FreeRTOS** que contenga las siguientes caracteristicas.

1. Una tarea que simule un sensor de temperatura. Generando valores aleatorios, con una frecuencia de 10 Hz.
2. Una tarea que reciba los valores del sensor y aplique un filtro pasa bajos. Donde cada valor resultante es el promedio de las ultimas N mediciones.
3. Una tarea que grafica en el display los valores de temperatura en el tiempo.
4. Se debe poder recibir comandos por la interfaz UART para cambiar el N del filtro.
5. Calcular el stack necesario para cada task. Realizar el analisis utilizando
`uxTaskGetStackHighWaterMark` o `vApplicationStackOverflowHook`.
6. Implementar una tarea tipo top de linux, que muestre periodicamente estadisticas de las tareas (uso de cpu, uso de memoria, etc)

---

## Implementacion
Este trabajo esta basado sobre la DEMO proporcionada desde la instalacion de [FreeRTOS](https://www.freertos.org/a00104.html), la cual nos da una base para poder comenzar a trabajar, con sus librerias y configuraciones necesarias.

Para poder correr el proyecto se necesita tener instalado [QEMU](https://www.qemu.org/download/) para emular nuestra placa y [gcc-arm-none-eabi](https://developer.arm.com/downloads/-/gnu-rm) para compilar el proyecto. Una vez instalados estos paquetes, se puede correr el proyecto utilizando el `build.sh` o con los siguientes comandos, dentro de nuestro directorio `DEMO`.

```bash
make clean
make
qemu-system-arm -machine lm3s811evb -kernel gcc/RTOSDemo.axf -serial stdio
```

Estas flags de QEMU nos permiten emular la placa en particular que estamos utilizando, cargar nuestro binario y utilizar la consola serial para interactuar con nuestra aplicación.

## Tasks
Para la implementación de las tareas, se crearon 4 tareas en total, una para cada requerimiento del enunciado (Sensor, Filtro, Graficador y Top).
