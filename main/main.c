
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

/*
ButtonControlsLight
Update of ReadPushButton

Incorporates a three led counter, counts from zero to seven, code borrowed from 'county' under assignment 01.  The push button controls the led count: single click increments the count, double click decrements, long press runs a bitwise animation, releasing long press resets the count to zero.  Had to add a partial logic dup to measure time and duration in a state machine, and poll it to detect a single click not being followed by double click.  TODO: clean up that mess, have one timeval object as a singleton.
*/

// Leds on output pins 2,3,4, pushbutton on input pin 5.
#define PIN_LIGHT_1 GPIO_NUM_2
#define PIN_LIGHT_2 GPIO_NUM_3
#define PIN_LIGHT_3 GPIO_NUM_4
#define PIN_BUTTON  GPIO_NUM_5

//====================
// Prototypes
//====================

// These need to maintain their 'static' declaration, but not the IRAM_ATTR attribution.

static void ButtonEventTask( void *params );
static void IsrHandler( void *args );
static void ButtonFirstDown( void );
static void ButtonSecondDown( void );
static void ButtonSingleClickDone( void );
static void ButtonDoubleClickDone( void );
static void ButtonLongPressDone( void );
static void CounterPollingTask( void *params );

//====================
// Button manager
//====================

static const char *TAG_BUTTON = "Button";
static const char *TAG_COUNTER = "Counter";

QueueHandle_t InterruptQueue;
unsigned long priorUpMs = 0;
unsigned long priorDownMs = 0;
int priorButtonState = 1;
int isSecondDown = 0;

// Setup gpio pin to read pushbutton.
static void ConfigureButton( void )
	{
	ESP_LOGI( TAG_BUTTON, "ConfigureButton(): resetting gpio pin to input" );
	// gpio_pad_select_gpio( PIN_BUTTON );
	gpio_set_direction( PIN_BUTTON, GPIO_MODE_INPUT );
	gpio_pulldown_dis( PIN_BUTTON );
	gpio_pullup_en( PIN_BUTTON );
	gpio_set_intr_type( PIN_BUTTON, GPIO_INTR_ANYEDGE );

	InterruptQueue = xQueueCreate( 10, sizeof( int ));
	xTaskCreate( ButtonEventTask, "ButtonEventTask", 2048, NULL, 1, NULL );

	gpio_install_isr_service( 0 );
	gpio_isr_handler_add( PIN_BUTTON, IsrHandler, (void *)PIN_BUTTON );
	}

// Interrupt routine responds to button state changes by (quickly) queuing events, for a task to read and process later.
static void IRAM_ATTR IsrHandler( void *args )
	{
	int pinNumber = (int)args;
	xQueueSendFromISR( InterruptQueue, &pinNumber, NULL );
	}

// Task to read button events from a queue, and interpret single clicks, double clicks, and long presses.
static void ButtonEventTask( void *params )
	{
	int pinNumber;
	struct timeval newTime;
	while( true )
		{
		// Wait for queue event, loop back and wait again on timeout.
		if( !xQueueReceive( InterruptQueue, &pinNumber, portMAX_DELAY )) continue;

		// Got a queue event, check elapsed time and button state.
		int newButtonState = gpio_get_level( PIN_BUTTON );
		if( newButtonState == priorButtonState )
			{
			// Ignore button bounce.
			continue;
			}
		gettimeofday( &newTime, NULL );
		unsigned long timeMs = 1000 * newTime.tv_sec + newTime.tv_usec / 1000;
		if( newButtonState == 1 )
			{
			// Button up.
			unsigned long holdTimeMs = timeMs - priorDownMs;
			if( holdTimeMs > 750 )
				{
				ESP_LOGI( TAG_BUTTON, "delayed up, release done: holdTimeMs %ld", holdTimeMs );
				ButtonLongPressDone( );
				}
			else
				{
				if( isSecondDown )
					{
					ESP_LOGI( TAG_BUTTON, "quick up, double done: holdTimeMs %ld", holdTimeMs );
					ButtonDoubleClickDone( );
					}
				else
					{
					ESP_LOGI( TAG_BUTTON, "quick up, click done: holdTimeMs %ld", holdTimeMs );
					ButtonSingleClickDone( );
					}
				}
			isSecondDown = 0;
			priorUpMs = timeMs;
			}
		else
			{
			// Button down.
			unsigned long pauseTimeMs = timeMs - priorUpMs;
			if( pauseTimeMs < 150 )
				{
				ESP_LOGI( TAG_BUTTON, "second down, double initiated: pauseTimeMs %ld", pauseTimeMs );
				isSecondDown = 1;
				ButtonSecondDown( );
				}
			else
				{
				ESP_LOGI( TAG_BUTTON, "first down, click/press initiated: pauseTimeMs %ld", pauseTimeMs );
				ButtonFirstDown( );
				}
			priorDownMs = timeMs;
			}
		priorButtonState = newButtonState;
		}
	}

//====================
// Led manager
//====================

// Led state is a bit field, lower three bits power three corresponding leds.
static uint8_t ledState = 0;

// Setup gpio pins to drive leds.
static void ConfigureLeds( void )
	{
	ESP_LOGI( TAG_COUNTER, "ConfigureLeds(): resetting gpio pins to output" );
	gpio_reset_pin( PIN_LIGHT_1 );
	gpio_reset_pin( PIN_LIGHT_2 );
	gpio_reset_pin( PIN_LIGHT_3 );
	gpio_set_direction( PIN_LIGHT_1, GPIO_MODE_OUTPUT );
	gpio_set_direction( PIN_LIGHT_2, GPIO_MODE_OUTPUT );
	gpio_set_direction( PIN_LIGHT_3, GPIO_MODE_OUTPUT );

	xTaskCreate( CounterPollingTask, "CounterPollingTask", 2048, NULL, 1, NULL );
	}

// Set the GPIO levels according to the lower three bits of ledState.
static void UpdateLeds( void )
	{
	gpio_set_level( PIN_LIGHT_1, ledState & 1 );
	gpio_set_level( PIN_LIGHT_2, ( ledState >> 1 ) & 1 );
	gpio_set_level( PIN_LIGHT_3, ( ledState >> 2 ) & 1 );
	}

//====================
// Counter manager
//====================

int cmState = 0;
unsigned long pressTimeMs = 0;
unsigned long releaseTimeMs = 0;

struct timeval cmTimeval;
static unsigned long cmGetCurrentTimeMs( void )
	{
	gettimeofday( &cmTimeval, NULL );
	return( 1000 * cmTimeval.tv_sec + cmTimeval.tv_usec / 1000 );
	}

static void ButtonFirstDown( void )
	{
	cmState = 1;
	pressTimeMs = cmGetCurrentTimeMs( );
	printf( "--> ButtonFirstDown() cmState %d, pressTimeMs %ld\n", cmState, pressTimeMs );
	}

static void ButtonSecondDown( void )
	{
	cmState = 2;
	pressTimeMs = cmGetCurrentTimeMs( );
	printf( "--> ButtonSecondDown() cmState %d, pressTimeMs %ld\n", cmState, pressTimeMs );
	}

static void ButtonLongPressDone( void )
	{
	cmState = 0;
	ledState = 0;
	UpdateLeds( );
	printf( "--> ButtonLongPressDone() cmState %d\n", cmState );
	}

static void ButtonDoubleClickDone( void )
	{
	cmState = 0;
	ledState = ( ledState + 7 ) % 8;
	UpdateLeds( );
	printf( "--> ButtonDoubleClickDone() cmState %d\n", cmState );
	}

static void ButtonSingleClickDone( void )
	{
	releaseTimeMs = cmGetCurrentTimeMs( );
	cmState = 4;
	printf( "--> ButtonSingleClickDone() cmState %d, releaseTimeMs %ld\n", cmState, releaseTimeMs );
	}

struct timeval pollTimeval;
static unsigned long pollGetCurrentTimeMs( void )
	{
	gettimeofday( &pollTimeval, NULL );
	return( 1000 * pollTimeval.tv_sec + pollTimeval.tv_usec / 1000 );
	}

static void CounterPollingTask( void *params )
	{
	// Poll at pace to update leds for "scrambling" behavior, which is more than quick enough to respond to button state updates.
	int pollPeriodMs = 80;
	//int pollPressCount = 0;
	//unsigned long priorPollTimeMs = pollGetCurrentTimeMs( );
	pressTimeMs = pollGetCurrentTimeMs( );

	while( 1 )
		{
		vTaskDelay( pollPeriodMs / portTICK_PERIOD_MS );
		unsigned long curPollTimeMs = pollGetCurrentTimeMs( );
		switch( cmState )
			{
			case 1:
			case 2:
				if( curPollTimeMs - pressTimeMs > 200 )
					{
					// The button has been down long enough to initiate a long press.
					printf( "--> CounterPollingTask() long press detected from cmState %d\n", cmState );
					cmState = 3;
					ledState = 4;
					UpdateLeds( );
					printf( "--> CounterPollingTask() mew cmState %d\n", cmState );
					}
				break;
			case 3:
				ledState = ledState >> 1;
				if( ledState == 0 ) ledState = 4;
				UpdateLeds( );
				break;
			case 4:
				// Button has been released from first click.
				if( curPollTimeMs - releaseTimeMs > 150 )
					{
					// Button release long enough to disqualify a double click, increment counter.
					printf( "--> CounterPollingTask() single click detected from cmState %d\n", cmState );
					cmState = 0;
					ledState = ( ledState + 1 ) % 8;
					UpdateLeds( );
					printf( "--> CounterPollingTask() new cmState %d\n", cmState );
					}
				break;
			default:
				break;
			}
		}
	}

//====================
// Main
//====================

void app_main( void )
	{
	int negMod = (-2) % 5;
	printf( "--> main() experiment -2 mod 5 eq %d\n", negMod );
	printf( "--> main() configuring button gpio pin\n" );
	ConfigureButton( );
	printf( "--> main() configuring led gpio pins\n" );
	ConfigureLeds( );
	printf( "--> main() done\n" );
	}

