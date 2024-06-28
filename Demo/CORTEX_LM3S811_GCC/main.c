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
#define mainCHECK_DELAY				( ( TickType_t ) 100 / portTICK_PERIOD_MS )		// 10 [Hz]
#define mainTOP_DELAY 				( ( TickType_t ) 1000 / portTICK_PERIOD_MS )
#define mainSENSOR_DELAY			( ( TickType_t ) 100 / portTICK_PERIOD_MS )

/* UART configuration - note this does not use the FIFO so is not very
efficient. */
#define mainBAUD_RATE				( 19200 )
#define mainFIFO_SET				( 0x10 )

/* Demo task priorities. */
#define mainCHECK_TASK_PRIORITY		( tskIDLE_PRIORITY + 3 )

/* Demo board specifics. */
#define mainPUSH_BUTTON             GPIO_PIN_4

/* Misc. */
#define mainQUEUE_SIZE				( 4 )
#define mainDEBOUNCE_DELAY			( ( TickType_t ) 150 / portTICK_PERIOD_MS )
#define mainNO_DELAY				( ( TickType_t ) 0 )
#define MAX_ARRAY_VALUE				20
#define MAX_X_AXIS					85
#define MAX_TEMP					30
#define MIN_TEMP					0

/*---------------------------TASKS---------------------------*/
static void vSensorTask( void *pvParameters );
static void vAverageTask( void *pvParameters );
static void vDisplayTask( void *pvParameters );

/*-------------------------PROTOTYPES------------------------*/
static void prvSetupHardware( void );
void prvSetupTimer( void );
static char* cGetCharacter( int value );
static int iGetRow( int value );
uint32_t uiGetRandomNumber( void );
void vPushValueIntoArray( int array[], int value, int size );
int iGetAverageTemperature( int array[], int size );
void vDrawAxis( void );
unsigned long ulGetHighFrequencyTimerTicks( void );
void Timer0IntHandler( void );

/*--------------------------GLOBALS--------------------------*/
UBaseType_t uxHighWaterMarkSensor;
TaskStatus_t *pxTaskStatusArray;
unsigned long ulHighFrequencyTimerTicks;

static int iActualTemperature = 15;
static uint32_t rseed = 0xDEADBEEF; 

/*---------------------------QUEUES--------------------------*/
QueueHandle_t xSensorQueue;
QueueHandle_t xAverageQueue;
QueueHandle_t xUARTQueue;

/*----------------------------INIT---------------------------*/
int main( void )
{
	/* Configure the clocks, UART and GPIO. */
	prvSetupHardware();

	/* Create the queues used in the project. */
	xSensorQueue = xQueueCreate( mainQUEUE_SIZE, sizeof( int ) );
	xAverageQueue = xQueueCreate( mainQUEUE_SIZE, sizeof( int ) );
	xUARTQueue = xQueueCreate( mainQUEUE_SIZE, sizeof( char* ) );

	/* Error handling. */
	if ((xSensorQueue == NULL) || (xAverageQueue == NULL) || (xUARTQueue == NULL))
    	while (true);

	/* Start the tasks defined within the file. */
	xTaskCreate( vSensorTask, "Sensor", configMINIMAL_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY + 1, NULL );
	xTaskCreate( vAverageTask, "Average", configMINIMAL_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY, NULL );
	xTaskCreate( vDisplayTask, "Display", configMINIMAL_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY - 1, NULL );

	/* Start the scheduler. */
	vTaskStartScheduler();

	/* Will only get here if there was insufficient heap to start the
	scheduler. */

	return 0;
}

/*---------------------------TASKS---------------------------*/
static void vSensorTask( void *pvParameters ) 
{
	TickType_t xLastExecutionTime = xTaskGetTickCount();

	/* Error handling. */
	if (uxTaskGetStackHighWaterMark(NULL) < 1)
		while (true);

	for(;;)
	{
		vTaskDelayUntil(&xLastExecutionTime, mainSENSOR_DELAY);

		if (uiGetRandomNumber() % 2 == 0)
			iActualTemperature++;
		else
			iActualTemperature--;

		if (xQueueSend(xSensorQueue, &iActualTemperature, portMAX_DELAY) != pdTRUE)
			while (true);

		/* Error handling. */
		if (uxTaskGetStackHighWaterMark(NULL) < 1)
			while (true);
	}
}

static void vAverageTask( void *pvParameters )
{
	int newTemperature;
	int averageTemperature;
	int temperatureArray[MAX_ARRAY_VALUE] = {};

	/* Error handling. */
	if (uxTaskGetStackHighWaterMark(NULL) < 1)
		while (true);

	for(;;)
	{
		/* Receive the value from the sensor. */
		xQueueReceive(xSensorQueue, &newTemperature, portMAX_DELAY);

		/* Add the value to my temperature array. */
		vPushValueIntoArray(temperatureArray, newTemperature, MAX_ARRAY_VALUE);

		/* Get the new average value. */
		averageTemperature = iGetAverageTemperature(temperatureArray, MAX_ARRAY_VALUE);

		/* Send the value to the display graph. */
		if (xQueueSend(xAverageQueue, &averageTemperature, portMAX_DELAY) != pdTRUE)
			while (true);
	
		/* Error handling. */
		if (uxTaskGetStackHighWaterMark(NULL) < 1)
			while (true);
	}
}

static void vDisplayTask( void *pvParameters )
{
	int temperatureArray[MAX_X_AXIS] = {};
	int averageTemperature;

	/* Error handling. */
	if (uxTaskGetStackHighWaterMark(NULL) < 1)
		while (true);

	for(;;)
	{
		/* Receive the value from the average task. */
		xQueueReceive(xAverageQueue, &averageTemperature, portMAX_DELAY);

		/* Add the value to my temperature array. */
		vPushValueIntoArray(temperatureArray, averageTemperature, MAX_X_AXIS);

		/* Draw the graph. */
		OSRAMClear();
		vDrawAxis();

		for (int i=0 ; i < MAX_ARRAY_VALUE ; i++)
			OSRAMImageDraw(cGetCharacter(temperatureArray[i]), i+11, iGetRow(temperatureArray[i]), 1, 1);

		/* Error handling. */
		if (uxTaskGetStackHighWaterMark(NULL) < 1)
			while (true);
	}
}


/*---------------------------CONFIG--------------------------*/
static void prvSetupHardware( void )
{
	/* Setup UART */
	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
	UARTConfigSet(UART0_BASE, mainBAUD_RATE, (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE));
	UARTIntEnable(UART0_BASE, UART_INT_RX);
	IntPrioritySet(INT_UART0, configKERNEL_INTERRUPT_PRIORITY);
	IntEnable(INT_UART0);

	/* Setup LCD */
	OSRAMInit(false);
	OSRAMStringDraw("www.FreeRTOS.org", 0, 0);
	OSRAMStringDraw("LM3S811 demo", 16, 1);
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

int iGetAverageTemperature( int array[], int size )
{
	int sum = 0;

	for (int i=0 ; i < size ; i++)
		sum += array[(size-1)-i];

	return sum / size;
}

void vDrawAxis( void )
{
	// Y Axis
	OSRAMImageDraw("", 9, 0, 2, 1);
	OSRAMImageDraw("", 9, 1, 2, 1);

	// X Axis
	for (int i=0 ; i<MAX_X_AXIS ; i++)
		OSRAMImageDraw("@", i+11, 1, 2, 1);

	OSRAMImageDraw("\021\025\037",0,0,3,1);		//Numero 3
	OSRAMImageDraw("\016\021\021\016",4,0,4,1);	//Numero 0
	OSRAMImageDraw("8DD8",4,1,4,1);				//Numero 0 inferior
}

static char* cGetCharacter( int value )
{
	if (value < 2)
		return "@";
	else if (value < 4)
		return "`";
	else if (value < 8)
		return "P";
	else if (value < 10)
		return "H";
	else if (value < 12)
		return "D";
	else if (value < 14)
		return "B";
	else if (value < 16)
		return "A";
	else if (value < 18)
		return "@";
	else if (value < 20)
		return " ";
	else if (value < 22)
		return "\020";
	else if (value < 24)
		return "\b";
	else if (value < 26)
		return "\004";
	else if (value < 28)
		return "\002";
	else if (value <= 30)
		return "\001";
}

static int iGetRow( int value )
{
	if(value > 16)
		return 0;
	return 1;
}

unsigned long ulGetHighFrequencyTimerTicks( void )
{
	return ulHighFrequencyTimerTicks;
}

/*---------------------------HANDLERS------------------------*/
void Timer0IntHandler( void )
{
	TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
	ulHighFrequencyTimerTicks++;
}