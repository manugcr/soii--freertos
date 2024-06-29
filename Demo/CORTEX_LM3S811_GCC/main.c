/*
 * FreeRTOS V202212.01
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/* Environment includes. */
#include "DriverLib.h"

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Delay between cycles of the 'check' task. */
#define mainSENSOR_DELAY			( ( TickType_t ) 100 / portTICK_PERIOD_MS )		// 10 [Hz]

/* UART configuration - note this does not use the FIFO so is not very
efficient. */
#define mainBAUD_RATE				( 19200 )

/* Demo task priorities. */
#define mainCHECK_TASK_PRIORITY		( tskIDLE_PRIORITY + 3 )

/* Misc. */
#define mainQUEUE_SIZE				( 3 )
#define MAX_ARRAY_VALUE				20
#define MAX_COLUMNS					85
#define MAX_TEMP					30
#define MIN_TEMP					0

/*---------------------------TASKS---------------------------*/
static void vSensorTask( void *pvParameters );
static void vAverageTask( void *pvParameters );
static void vDisplayTask( void *pvParameters );

/*-------------------------PROTOTYPES------------------------*/
static void prvSetupHardware( void );
static char* cGetCharacter( int value );
void prvSetupTimer( void );
void vPushValueIntoArray( int array[], int value, int size );
void Timer0IntHandler( void );
void vDrawAxis( void );
void vCheckStackOverflow(void);
int iGetAverageTemperature( int array[], int num, int size );
uint32_t uiGetRandomNumber( void );
unsigned long ulGetHighFrequencyTimerTicks( void );

/*--------------------------GLOBALS--------------------------*/
static int iActualTemperature = 15;
static uint32_t rseed = 1; 
unsigned long ulHighFrequencyTimerTicks;

/*---------------------------QUEUES--------------------------*/
QueueHandle_t xSensorQueue;
QueueHandle_t xAverageQueue;

/*----------------------------INIT---------------------------*/
int main( void )
{
	/* Configure the clocks, UART and GPIO. */
	prvSetupHardware();

	/* Create the queues used in the project. */
	xSensorQueue = xQueueCreate( mainQUEUE_SIZE, sizeof( int ) );
	xAverageQueue = xQueueCreate( mainQUEUE_SIZE, sizeof( int ) );

	/* Error handling. */
	if ((xSensorQueue == NULL) || (xAverageQueue == NULL))
	{
		OSRAMClear();
		OSRAMStringDraw("Queue Error", 0, 0);
    	while (true);
	}

	/* Start the tasks defined within the file. */
	xTaskCreate( vSensorTask, "Sensor", configMINIMAL_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY + 1, NULL );
	xTaskCreate( vAverageTask, "Average", configMINIMAL_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY, NULL );
	xTaskCreate( vDisplayTask, "Display", configMINIMAL_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY - 1, NULL );

	/* Start the scheduler. */
	vTaskStartScheduler();

	/* Will only get here if there was insufficient heap to start the
	scheduler. */
	vCheckStackOverflow();

	return 0;
}

/*---------------------------TASKS---------------------------*/
static void vSensorTask( void *pvParameters ) 
{
	TickType_t xLastExecutionTime= xTaskGetTickCount();
	int change;

	/* Error handling. */
	vCheckStackOverflow();

	while (true)
	{
		vTaskDelayUntil(&xLastExecutionTime, mainSENSOR_DELAY);

		change = (uiGetRandomNumber() % 3) - 1; // -1, 0, 1
		iActualTemperature += change;

		if (iActualTemperature > MAX_TEMP)
			iActualTemperature = MAX_TEMP;
		if (iActualTemperature < MIN_TEMP)
			iActualTemperature = MIN_TEMP;

		xQueueSend(xSensorQueue, &iActualTemperature, portMAX_DELAY);

		/* Error handling. */
		vCheckStackOverflow();
	}
}

static void vAverageTask( void *pvParameters )
{
	int newTemperature;
	int averageTemperature;
	int temperatureArray[MAX_ARRAY_VALUE] = {};
	int num = 5;

	/* Error handling. */
	vCheckStackOverflow();

	while (true)
	{
		/* Receive the value from the sensor. */
		xQueueReceive(xSensorQueue, &newTemperature, portMAX_DELAY);

		/* Add the value to my temperature array. */
		vPushValueIntoArray(temperatureArray, newTemperature, MAX_ARRAY_VALUE);

		/* Get the new average value. */
		averageTemperature = iGetAverageTemperature(temperatureArray, num, MAX_ARRAY_VALUE);

		/* Send the value to the display graph. */
		xQueueSend(xAverageQueue, &averageTemperature, portMAX_DELAY);
	
		/* Error handling. */
		vCheckStackOverflow();
	}
}

static void vDisplayTask( void *pvParameters )
{
	int temperatureArray[MAX_COLUMNS] = {};
	int averageTemperature;

	/* Error handling. */
	vCheckStackOverflow();

	while (true)
	{
		/* Receive the value from the average task. */
		xQueueReceive(xAverageQueue, &averageTemperature, portMAX_DELAY);

		/* Add the value to my temperature array. */
		vPushValueIntoArray(temperatureArray, averageTemperature, MAX_COLUMNS);

		/* Draw the graph. */
		OSRAMClear();
		vDrawAxis();

		for (int i=0 ; i < MAX_COLUMNS ; i++)
		{
            char *character = cGetCharacter(temperatureArray[i]);
			int row = (temperatureArray[i] > 16) ? 0 : 1;
            OSRAMImageDraw(character, i + 11, row, 1, 1);
		}

		/* Error handling. */
		vCheckStackOverflow();
	}
}


/*---------------------------CONFIG--------------------------*/
static void prvSetupHardware( void )
{
	/* Setup UART */
	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
	UARTConfigSet(UART0_BASE, mainBAUD_RATE, (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE));

	/* Setup LCD */
	OSRAMInit(false);
	OSRAMStringDraw("www.FreeRTOS.org", 0, 0);
	OSRAMStringDraw("SOII Project", 16, 1);
}

void prvSetupTimer( void )
{
	SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
	IntMasterEnable();
	TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
	TimerConfigure(TIMER0_BASE,TIMER_CFG_32_BIT_TIMER);
	TimerLoadSet(TIMER0_BASE, TIMER_A, 1500);
	TimerIntRegister(TIMER0_BASE,TIMER_A, Timer0IntHandler);
	TimerEnable(TIMER0_BASE,TIMER_A);
}

/*----------------------------UTILS--------------------------*/
uint32_t uiGetRandomNumber( void )
{
	rseed = rseed *1103515245+12345;
	return (uint32_t) (rseed/131072)%65536;
}

void vPushValueIntoArray( int array[], int value, int size )
{
	for (int i=0 ; i < size-1 ; i++)
		array[i] = array[i+1];
	
	array[size-1] = value;
}

int iGetAverageTemperature( int array[], int num, int size )
{
	int sum = 0;
	
	if (num > size)
		num = size;

	for (int i=0 ; i < num ; i++)
		sum += array[(size-1)-i];

	return sum / num;
}

void vDrawAxis( void )
{
	// Y Axis
	OSRAMImageDraw("\177", 10, 0, 2, 1);
	OSRAMImageDraw("\177", 10, 1, 2, 1);

	// X Axis
	for (int i=0 ; i<MAX_COLUMNS ; i++)
		OSRAMImageDraw("@", i+11, 1, 2, 1);

	OSRAMImageDraw("8DD8",4,1,4,1);				// Number 0
}

static char* cGetCharacter( int value )
{
	if (value < 2) 			return "@";
	else if (value < 4) 	return "`";
	else if (value < 8) 	return "P";
	else if (value < 10) 	return "H";
	else if (value < 12) 	return "D";
	else if (value < 14) 	return "B";
	else if (value < 16) 	return "A";
	else if (value < 18) 	return "@";
	else if (value < 20) 	return " ";
	else if (value < 22) 	return "\020";
	else if (value < 24) 	return "\b";
	else if (value < 26) 	return "\004";
	else if (value < 28) 	return "\002";
	else if (value <= 30)	return "\001";
}

unsigned long ulGetHighFrequencyTimerTicks( void )
{
	return ulHighFrequencyTimerTicks;
}

void vCheckStackOverflow(void)
{
    if (uxTaskGetStackHighWaterMark(NULL) < 1)
	{
		OSRAMClear();
		OSRAMStringDraw("Stack Overflow", 0, 0);
        while (true);
	}
}

/*---------------------------HANDLERS------------------------*/
void Timer0IntHandler( void )
{
	TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
	ulHighFrequencyTimerTicks++;
}