/* Standard includes. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Scheduler includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "altera_avalon_pio_regs.h" 	// to use PIO functions
#include "sys/alt_irq.h"              	// to register interrupts

// task priorities
#define MAINTENANCE_TASK_PRIORITY 5
#define SWITCH_POLLING_TASK_PRIORITY 6

// function definitions
static void ToggleMaintenanceTask(void *pvParameters);
static void SwitchPollingTask(void *pvParameters);

// semaphores
SemaphoreHandle_t maintenance_sem;

void button_interrupts_function(void* context, alt_u32 id)
{
	// need to cast the context first before using it
	int* temp = (int*) context;
	(*temp) = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE);

	// clears the edge capture register
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x1);

	printf("maintenance button pressed \n");

	xSemaphoreGiveFromISR(maintenance_sem, pdFALSE);
}

/*
 * Create the demo tasks then start the scheduler.
 */


int main(void)
{
	// switch polling setup
	unsigned int uiSwitchValue = 0;
	IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, 0x0);
	IOWR_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE, 0x0);

	// maintenance setup
	maintenance_sem = xSemaphoreCreateBinary();
	int buttonValue = 0;
	// clears the edge capture register. Writing 1 to bit clears pending interrupt for corresponding button.
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);
	IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, 0x0);
	// enable interrupts for first button
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE, 0x1);
	// register the ISR
	alt_irq_register(PUSH_BUTTON_IRQ,(void*)&buttonValue, button_interrupts_function);



	// Tasks
	xTaskCreate( ToggleMaintenanceTask, "Maintenance Task", configMINIMAL_STACK_SIZE, &buttonValue, MAINTENANCE_TASK_PRIORITY, NULL);
	xTaskCreate( SwitchPollingTask, "Switch Polling Task", configMINIMAL_STACK_SIZE, &uiSwitchValue, SWITCH_POLLING_TASK_PRIORITY, NULL);

	/* Finally start the scheduler. */
	vTaskStartScheduler();

	/* Will only reach here if there is insufficient heap available to start
	the scheduler. */
	for (;;);
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
	    temp = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE);
	    // write the value of the switches to the red LEDs
	    IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, *temp);
	    printf("%u", *temp);

	    // delay for 100ms
		vTaskDelay(1000);
	}
}
