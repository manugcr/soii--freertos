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
#define mainTOP_DELAY				( ( TickType_t ) 2000 / portTICK_PERIOD_MS )

/* UART configuration - note this does not use the FIFO so is not very
efficient. */
#define mainBAUD_RATE				( 19200 )

/* Demo task priorities. */
#define mainCHECK_TASK_PRIORITY		( tskIDLE_PRIORITY + 3 )

/* Misc. */
#define mainQUEUE_SIZE				( 3 )
#define MAX_ARRAY_VALUE				20
#define MAX_COLUMNS					85			// 85 columns for the LCD
#define MAX_TEMP					30
#define MIN_TEMP					0


/*---------------------------TASKS---------------------------*/
static void vSensorTask( void *pvParameters );
static void vAverageTask( void *pvParameters );
static void vDisplayTask( void *pvParameters );
static void vTopTask( void *pvParameters );


/*-------------------------PROTOTYPES------------------------*/
static void prvSetupHardware( void );
static char* cGetColumnOctal( int value );
void prvSetupTimer( void );
void vPushValueIntoArray( int array[], int value, int size );
void Timer0IntHandler( void );
void vDrawAxis( void );
void vCheckStackOverflow(void);
void vPrintTopStats( void );
void vSendStringToUART( const char* );
char* cUnsignedIntToString( unsigned, char*, int );
int iGetAverageTemperature( int array[], int bufferSize, int arraySize );
unsigned long ulGetHighFrequencyTimerTicks( void );
uint32_t uiGetRandomNumber( void );


/*--------------------------GLOBALS--------------------------*/
static int iActualTemperature = 15;
static uint32_t rseed = 0xDEADBEEF; 
unsigned long ulHighFrequencyTimerTicks;
TaskStatus_t *pxTaskStatusArray;


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
	xTaskCreate( vTopTask, "Top", configMINIMAL_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY - 2, NULL );

	/* Start the scheduler. */
	vTaskStartScheduler();

	/* Will only get here if there was insufficient heap to start the
	scheduler. */
	vCheckStackOverflow();

	return 0;
}


/*---------------------------TASKS---------------------------*/
/**
 * @brief The task that simulates a temperature sensor.
 * 
 * This is just a simulation of a temperature sensor that changes the temperature randomly.
 * There is not a real sensor in this project.
 * 
 * @param pvParameters Parameters passed to the task (not used).
 */
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

/**
 * @brief The task that calculates the average temperature.
 * 
 * @param pvParameters Parameters passed to the task (not used).
 */
static void vAverageTask( void *pvParameters )
{
	int newTemperature;
	int averageTemperature;
	int temperatureArray[MAX_ARRAY_VALUE] = {};
	int bufferSize = 5;

	/* Error handling. */
	vCheckStackOverflow();

	while (true)
	{
		/* Receive the value from the sensor. */
		xQueueReceive(xSensorQueue, &newTemperature, portMAX_DELAY);

		/* Add the value to my temperature array. */
		vPushValueIntoArray(temperatureArray, newTemperature, MAX_ARRAY_VALUE);

		/* Get the new average value. */
		averageTemperature = iGetAverageTemperature(temperatureArray, bufferSize, MAX_ARRAY_VALUE);

		/* Send the value to the display graph. */
		xQueueSend(xAverageQueue, &averageTemperature, portMAX_DELAY);
	
		/* Error handling. */
		vCheckStackOverflow();
	}
}

/**
 * @brief The task that displays the temperature graph.
 * 
 * It receives the average temperature from the average task and displays it on the LCD as a graph.
 * 
 * @param pvParameters Parameters passed to the task (not used).
 */
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
            char *character = cGetColumnOctal(temperatureArray[i]);
			int row = (temperatureArray[i] > 16) ? 0 : 1;
            OSRAMImageDraw(character, i + 11, row, 1, 1);
		}

		/* Error handling. */
		vCheckStackOverflow();
	}
}

/**
 * @brief The task that prints system statistics.
 * 
 * It prints the TASK, CPU%, STACK FREE and TICKS of each task.
 * Sends it via UART.
 * 
 * @param pvParameters Parameters passed to the task (not used).
 */
static void vTopTask( void *pvParameters )
{
	UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
	pxTaskStatusArray = pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));

	/* Error handling. */
	vCheckStackOverflow();

	while (true)
	{
		vTaskDelay(mainTOP_DELAY);

		vPrintTopStats();

		/* Error handling. */
		vCheckStackOverflow();
	}
}


/*---------------------------CONFIG--------------------------*/
/**
 * @brief Sets up the hardware peripherals.
 * 
 * Sets up the UART and the LCD display.
 */
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

/**
 * @brief Configures the timer.
 */
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
/**
 * @brief Gets a random number.
 * 
 * Function obtained from the documentation of FreeRTOS on rand.c
 * 
 * @return A random number.
 */
uint32_t uiGetRandomNumber( void )
{
	rseed = rseed *1103515245+12345;
	return (uint32_t) (rseed/131072)%65536;
}

/**
 * @brief Pushes a value into an array, shifting existing values.
 * 
 * @param array The array to push the value into.
 * @param value The value to push.
 * @param size The size of the array.
 */
void vPushValueIntoArray( int array[], int value, int size )
{
	for (int i=0 ; i < size-1 ; i++)
		array[i] = array[i+1];
	
	array[size-1] = value;
}

/**
 * @brief Calculates the average temperature from an array.
 * 
 * This function calculates the average temperature in a circular buffer.
 * 
 * @param array The array of temperatures.
 * @param bufferSize The number of recent values to average.
 * @param arraySize The size of the array.
 * @return The average temperature.
 */
int iGetAverageTemperature( int array[], int bufferSize, int arraySize )
{
	int sum = 0;
	
	if (bufferSize > arraySize)
		bufferSize = arraySize;

	for (int i=0 ; i < bufferSize ; i++)
		sum += array[(arraySize-1)-i];

	return sum / bufferSize;
}

/**
 * @brief Draws the axis on the display.
 * 
 * OSRAMImageDraw( const char *pcImage, int x, int y, int width, int height )
 * 
 * Each character is a representation of an octal or ascii value, that is then converted to binary.
 * The image data is organized such that each byte represents a column of 8 pixels. 
 * The LSB is the topmost pixel, and the MSB is the bottommost pixel.
 * 
 * So for example \177 = 01111111
 * 
 * Or if i want to draw the number 0:
 * \070 = 00111000 -> First columnn
 * \104 = 01000100 -> Second column
 * \104 = 01000100 -> Third column
 * \070 = 00111000 -> Fourth column
 */
void vDrawAxis( void )
{
	// Y Axis
	OSRAMImageDraw("\377", 10, 0, 1, 1);
	OSRAMImageDraw("\377", 10, 1, 1, 1);

	// X Axis
	for (int i=0 ; i<MAX_COLUMNS ; i++)
		OSRAMImageDraw("\200", i+11, 1, 1, 1); 			// +11 to skip the Y Axis

	OSRAMImageDraw("\021\025\037", 0, 0, 3, 1);			// Number 3
	OSRAMImageDraw("\016\021\021\016", 4, 0, 4, 1);		// Number 0
	OSRAMImageDraw("\070\104\104\070", 4, 1, 4, 1);		// Number 0
}

/**
 * @brief Gets the octal representation of a column value.
 * 
 * The graph is divided in two rows of 8 pixels each.
 * So for the first row (bottom one) we need to graph the value of the x Axis and the dot of the graph.
 * For the second row (top one) we need to graph just the dot of the graph.
 * 
 * So for example if we have a temperature of 10, the binary representation of the column would be:
 * 1001000 = \220 -> Beign the first 1 the x Axis and the second 1 the dot of the graph.
 * 
 * @param value The column value.
 * @return A pointer to the octal representation string.
 */
static char* cGetColumnOctal( int value )
{
	if (value < 2) 			return "\200";
	else if (value < 4) 	return "\300";
	else if (value < 8) 	return "\240";
	else if (value < 10) 	return "\220";
	else if (value < 12) 	return "\210";
	else if (value < 14) 	return "\204";
	else if (value < 15) 	return "\202";
	else if (value < 16) 	return "\201"; 		// Last value of first row
	else if (value < 20) 	return "\200";
	else if (value < 22) 	return "\100";
	else if (value < 24) 	return "\040";
	else if (value < 25) 	return "\020";
	else if (value < 26) 	return "\010";
	else if (value < 28) 	return "\004";
	else if (value < 29) 	return "\002";
	else if (value <= 30)	return "\001";		// Last value of second row
}

/**
 * @brief Gets the high frequency timer ticks.
 * 
 * @return The high frequency timer ticks.
 */
unsigned long ulGetHighFrequencyTimerTicks( void )
{
	return ulHighFrequencyTimerTicks;
}

/**
 * @brief Checks for stack overflow and handles it.
 */
void vCheckStackOverflow(void)
{
    if (uxTaskGetStackHighWaterMark(NULL) < 1)
	{
		OSRAMClear();
		OSRAMStringDraw("Stack Overflow", 0, 0);
        while (true);
	}
}

/**
 * @brief Converts an unsigned integer to a string representation in the specified base.
 * 
 * @param value The unsigned integer value to convert.
 * @param dest The destination buffer to store the string representation.
 * @param base The base for the conversion (between 2 and 36).
 * @return A pointer to the destination buffer.
 */
char* cUnsignedIntToString(unsigned value, char *dest, int base) 
{
    const char *digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    char *start = dest;
    char *end;
    unsigned remainder;
    
    // Validate base
    if (base < 2 || base > 36) 
	{
        dest[0] = '\0';
        return NULL;
    }

    // Convert number to string in reverse order
    do {
        remainder = value % base;
        *dest++ = digits[remainder];
        value /= base;
    } while (value != 0);

    // Null-terminate the string
    *dest = '\0';

    // Reverse the string
    for (end=dest-1 ; start<end ; ++start,--end) 
	{
        char temp = *start;
        *start = *end;
        *end = temp;
    }

    return dest;
}

/**
 * @brief Sends a null-terminated string to the UART.
 * 
 * @param message The string to send.
 */
void vSendStringToUART( const char *message )
{
	while(*message != '\0')
    	UARTCharPut(UART0_BASE, *message++);

  	UARTCharPut(UART0_BASE, '\0');
}

/**
 * @brief Prints the task statistics to the UART.
 */
void vPrintTopStats(void) 
{
    volatile UBaseType_t uxArraySize;
    volatile UBaseType_t x;

    unsigned long ulTotalRunTime;
    unsigned long ulStatsAsPercentage;

	char counter[12];
	char percentage[12];
	char stack[12];

    if (pxTaskStatusArray != NULL) 
	{
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);

        ulTotalRunTime /= 100UL;

        vSendStringToUART("\r");

        if (ulTotalRunTime > 0) 
		{
            vSendStringToUART("TASK\tCPU%\tSTACK FREE\tTICKS\r\n");
			vSendStringToUART("---------------------------------------\r\n");

            for (x = 0; x < uxArraySize; x++) 
			{
                ulStatsAsPercentage = pxTaskStatusArray[x].ulRunTimeCounter / ulTotalRunTime;

                cUnsignedIntToString(pxTaskStatusArray[x].ulRunTimeCounter, counter, 10);
                cUnsignedIntToString(ulStatsAsPercentage, percentage, 10);
                cUnsignedIntToString(pxTaskStatusArray[x].usStackHighWaterMark, stack, 10);

                vSendStringToUART(pxTaskStatusArray[x].pcTaskName);
                vSendStringToUART("\t");
                vSendStringToUART(ulStatsAsPercentage > 0 ? percentage : "<1");
                vSendStringToUART("%\t");
                vSendStringToUART(stack);
                vSendStringToUART("\t\t");
                vSendStringToUART(counter);
                vSendStringToUART("\r\n");
            }

            vSendStringToUART("\r\n\r\n\r\n");
        }
    }
}


/*---------------------------HANDLERS------------------------*/
/**
 * @brief The interrupt handler for Timer0.
 */
void Timer0IntHandler( void )
{
	TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
	ulHighFrequencyTimerTicks++;
}