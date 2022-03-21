/* Standard includes. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Scheduler includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include "altera_avalon_pio_regs.h" 	// to use PIO functions
#include "sys/alt_irq.h"              	// to register interrupts

// task priorities
#define MAINTENANCE_TASK_PRIORITY 5
#define SWITCH_POLLING_TASK_PRIORITY 6

// function definitions
static void ToggleMaintenanceTask(void *pvParameters);
static void SwitchPollingTask(void *pvParameters);
void relay_control(TimerHandle_t xTimer);
void switchPollInit();
void maintanenceInit();

// global variables
unsigned int uiSwitchValue = 0;
int buttonValue = 0;
int relay_state = 31;
int NUM_OF_RELAYS = 5;
int num_of_blocked_relays = 0;
int RELAY_MASK = 31; // limits relay to 5 switches


// semaphores
SemaphoreHandle_t maintenance_sem;

int find_right_most_bit(int n) {
    return log2(n & -n) + 1;
}


void button_interrupts_function(void* context, alt_u32 id)
{
	// need to cast the context first before using it
	int* temp = (int*) context;
	(*temp) = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE);

	// clears the edge capture register
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, *temp);

	printf("maintenance button pressed \n");

	xSemaphoreGiveFromISR(maintenance_sem, pdFALSE);
}

void voltage_reading_isr(void* context, alt_u32 id) {
	int reading = 50;
	if (reading < 48 || reading >= 52) {

	}
}







int main(void)
{
	// set up
	maintenance_sem = xSemaphoreCreateBinary();

	switchPollInit();
	maintanenceInit();

	// set up timer
	TimerHandle_t relay_timer = xTimerCreate("Relay Timer" , pdMS_TO_TICKS(500), pdFALSE, 1, relay_control);
	xTimerStart(relay_timer, 10);

	// Tasks
	xTaskCreate( ToggleMaintenanceTask, "Maintenance Task", configMINIMAL_STACK_SIZE, &buttonValue, MAINTENANCE_TASK_PRIORITY, NULL);
	xTaskCreate( SwitchPollingTask, "Switch Polling Task", configMINIMAL_STACK_SIZE, &uiSwitchValue, SWITCH_POLLING_TASK_PRIORITY, NULL);

	/* Finally start the scheduler. */
	vTaskStartScheduler();

	/* Will only reach here if there is insufficient heap available to start
	the scheduler. */
	for (;;);
}

void relay_control(TimerHandle_t xTimer) {
	printf("relay_control:: timer callback...\n");

	// if not stable
	if (relay_state == 1) {
		// call task for turning off relays
		printf("turning off next relay : %d\n", find_right_most_bit(relay_state));
	} else {
		// call task for turning back on relays
		printf("turning on next relay: %d\n", find_right_most_bit(relay_state) - 1);
	}

	// if all relays are not back on reset timer.
	if (num_of_blocked_relays > 0) {
		printf("resting timer as relays are not all unblocked.\n");
		xTimerReset(xTimer, 10);
	} else {
		printf("all relays are back online!\n");
	}
}

void maintanenceInit() {
	// clears the edge capture register. Writing 1 to bit clears pending interrupt for corresponding button.
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);
	IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, 0x0);
	// enable interrupts for first button
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE, 0x1);

	// register the ISR
	alt_irq_register(PUSH_BUTTON_IRQ,(void*)&buttonValue, button_interrupts_function);
}

void switchPollInit() {
	// switch polling setup
	IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, 0x0);
	IOWR_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE, 0x0);
}


static void ToggleMaintenanceTask(void *pvParameters) {
	while (1)
	{
		if (xSemaphoreTake(maintenance_sem, ( TickType_t ) 10 )) {
			printf("Maintenance Task \n");

			int* temp = (int*) pvParameters;
			IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, *temp);
		}
	}
}

static void SwitchPollingTask(void *pvParameters) {
	while (1)
	{
		printf("polling switches");
		unsigned int* temp = (unsigned int*) pvParameters;

	    // read the value of the switch and store to uiSwitchValue
	    *temp = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE);
	    // write the value of the switches to the red LEDs
	    IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, *temp & RELAY_MASK);
	    printf("%u", *temp & RELAY_MASK);
	    printf("%u", *temp);
	    printf("%u", RELAY_MASK);

	    // delay for 100ms
		vTaskDelay(1000);
	}
}
