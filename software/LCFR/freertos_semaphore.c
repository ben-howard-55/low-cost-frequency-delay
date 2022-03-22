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
#define RELAY_MANAGER_TASK_PRIORITY 4

// function definitions
static void ToggleMaintenanceTask(void *pvParameters);
static void RelayLoadManagementTask(void *pvParameters);
static void SwitchPollingTask(void *pvParameters);

void relay_control_callback(TimerHandle_t xTimer);

void switchPollInit();
void maintanenceInit();

// global variables
unsigned int uiSwitchValue = 0;
unsigned int uiButtonValue = 0;
int relay_value_mask = 0;
int relay_volatility_state = 0;
int NUM_OF_RELAYS = 5;
int blocked_relay_mask = 31; // 31 -> block no relays
int RELAY_MASK = 31; // limits relay to 5 switches

// global timer
TimerHandle_t relay_timer;

// semaphores
SemaphoreHandle_t maintenance_sem;
SemaphoreHandle_t relay_manage_sem;

int find_right_most_bit(int n) {
	if (n == 0) return 0;
	return log2(n & -n) + 1;
}

void button_interrupts_function(void* context, alt_u32 id) {
	// need to cast the context first before using it
	int* temp = (int*) context;
	(*temp) = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE);

	// clears the edge capture register
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);

	if (*temp == 1) {
		printf("maintenance button pressed \n");
		xSemaphoreGiveFromISR(maintenance_sem, pdFALSE);
	} else if (*temp == 2) {
		printf("volatility button pressed \n");
		relay_volatility_state = 1;

		xSemaphoreGiveFromISR(relay_manage_sem, pdFALSE);
	} else if (*temp == 4) {
		printf("un-volatility button pressed \n");
		relay_volatility_state = 0;

		xSemaphoreGiveFromISR(relay_manage_sem, pdFALSE);
	}
}

void FrequencyAnalyzerInterrupt(void* context, alt_u32 id) {
	int* temp = (int*) context;

	// determine whether passed value is bad
	if (*temp == 1) {
		// check if timer active
		if (xTimerIsTimerActive(relay_timer) != pdFALSE) {
			// If active reset timer
			BaseType_t xHigherPriorityTaskWoken = pdFALSE;

			xTimerResetFromISR(relay_timer, &xHigherPriorityTaskWoken);
		} else {
			// call relay_control task
			xSemaphoreGiveFromISR(relay_manage_sem, pdFALSE);
		}
	}
}

int main(void) {
	// set up sems
	maintenance_sem = xSemaphoreCreateBinary();
	relay_manage_sem = xSemaphoreCreateBinary();

	switchPollInit();
	maintanenceInit();

	// set up timer
	relay_timer = xTimerCreate("Relay Timer", pdMS_TO_TICKS(5000), pdFALSE, 1,
			relay_control_callback);

	// Tasks
	xTaskCreate( ToggleMaintenanceTask, "Maintenance Task",
			configMINIMAL_STACK_SIZE, &uiButtonValue, MAINTENANCE_TASK_PRIORITY,
			NULL);
	xTaskCreate( SwitchPollingTask, "Switch Polling Task",
			configMINIMAL_STACK_SIZE, &uiSwitchValue,
			SWITCH_POLLING_TASK_PRIORITY, NULL);
	xTaskCreate( RelayLoadManagementTask, "Relay Manager Task",
			configMINIMAL_STACK_SIZE, NULL, RELAY_MANAGER_TASK_PRIORITY, NULL);

	/* Finally start the scheduler. */
	vTaskStartScheduler();

	/* Will only reach here if there is insufficient heap available to start
	 the scheduler. */
	for (;;)
		;
}

// timer callback
void relay_control_callback(TimerHandle_t xTimer) {
	printf("relay_control:: timer callback...\n");
	xSemaphoreGiveFromISR(relay_manage_sem, pdFALSE);
}

void maintanenceInit() {
	// clears the edge capture register. Writing 1 to bit clears pending interrupt for corresponding button.
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);
	IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, 0x0);
	// enable interrupts for first two buttons (for now)
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE, 0x7);

	// register the ISR
	alt_irq_register(PUSH_BUTTON_IRQ, (void*) &uiButtonValue,
			button_interrupts_function);
}

void switchPollInit() {
	// switch polling setup
	IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, 0x0);
	IOWR_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE, 0x0);
}

static void RelayLoadManagementTask(void *pvParameters) {
	while (1) {
		// wait for semaphore release
		if (xSemaphoreTake(relay_manage_sem, ( TickType_t ) 10 )) {
			// if timer active reset
			if (xTimerIsTimerActive(relay_timer) != pdFALSE) {
				printf("reseting timer as still active\n");
				BaseType_t xHigherPriorityTaskWoken = pdFALSE;
				xTimerReset(relay_timer, 10);
			} else {
				if (relay_volatility_state == 1) {
					// if volatile -> remove a relay
					printf("still volatile removing right most on relay: %d\n", relay_value_mask);
					int index = 0;
					if (relay_value_mask != 0) {
						index = find_right_most_bit(relay_value_mask);
					}

					if (index != 0) {
						printf("removing relay: %d\n", index);
						relay_value_mask = (relay_value_mask ^ (int) pow(2, index));
						blocked_relay_mask = relay_value_mask;
					} else {
						printf("All relays are already off.\n");
					}

					printf("resting timer as state is still bad.\n");
					xTimerReset(relay_timer, 10);

				} else {
					// not volatile so add relay
					int relay = find_right_most_bit(blocked_relay_mask);

					printf("Turning on relay: %d ", relay);
					if (blocked_relay_mask != 31) {
						if (relay == 0) {
							int rm = (int) pow(2, NUM_OF_RELAYS-1);
							// if no right bit => set 5th index to on. (xor)
							relay_value_mask = relay_value_mask ^ rm;
							blocked_relay_mask = blocked_relay_mask ^ rm;
						} else {
							int rm = (int) pow(2, relay-1);
							// set index - 1 on. (or)
							blocked_relay_mask = blocked_relay_mask | rm;
							relay_value_mask = relay_value_mask | rm;
						}
					}
					printf("reseting timer as not all relays are switched back on.\n");
					xTimerReset(relay_timer, 10);
				}
				relay_value_mask = relay_value_mask & RELAY_MASK;
				blocked_relay_mask = blocked_relay_mask & RELAY_MASK;

				// set LEDS
				printf("blocked relay number: %d ", blocked_relay_mask);
				printf("relay value: %d ", relay_value_mask);
				printf("relay actual: %d\n", relay_value_mask & blocked_relay_mask);
				IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE,
						relay_value_mask & blocked_relay_mask);
			}
		}
	}
}

static void ToggleMaintenanceTask(void *pvParameters) {
	while (1) {
		if (xSemaphoreTake(maintenance_sem, ( TickType_t ) 10 )) {
			printf("Maintenance Task \n");

			int* temp = (int*) pvParameters;
			IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, *temp);
		}
	}
}

static void SwitchPollingTask(void *pvParameters) {
	while (1) {
		unsigned int* temp = (unsigned int*) pvParameters;

		// read the value of the switch and store to uiSwitchValue
		*temp = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE);

		if (relay_volatility_state == 0) {
			// write the value of the switches to the red LEDs
//			printf("reading switches");
			IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, *temp & RELAY_MASK);

			relay_value_mask = *temp;
		} else {
			// write the value of the switches to the red LEDs
//			IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, *temp & blocked_relay_mask);
		}

		// delay for 100ms
		vTaskDelay(1000);
	}
}
