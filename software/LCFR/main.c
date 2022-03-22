/* Standard includes. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

/* Scheduler includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include "altera_avalon_pio_regs.h" 	// to use PIO functions
#include "sys/alt_irq.h"              	// to register interrupts
// task priorities
#define MAINTENANCE_TASK_PRIORITY 3
#define SWITCH_POLLING_TASK_PRIORITY 3
#define LOAD_MANAGER_TASK_PRIORITY 8

// global constants
const int LOAD_MASK = 31; // limits load to 5 switches
const int NUM_OF_LOADS = 5;

// function definitions
static void ToggleMaintenanceTask(void *pvParameters);
static void LoadManagementTask(void *pvParameters);
static void SwitchPollingTask(void *pvParameters);

void load_control_callback(TimerHandle_t xTimer);

void switchPollInit();
void maintanenceInit();

// input global variables
unsigned int uiSwitchValue = 0;
unsigned int uiButtonValue = 0;

// system state global variables.
bool load_volatility_state = false;
bool maintenance_state = false;
bool load_control_state = false;

int load_value = 0;
int blocked_load_mask = 31; // 31 -> block no LOADS

// global timer
TimerHandle_t load_timer;

// semaphores
SemaphoreHandle_t maintenance_sem;
SemaphoreHandle_t load_manage_sem;

int find_right_most_bit(int n) {
	if (n == 0)
		return 0;
	return n & -n;
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
		load_volatility_state = true;
		load_control_state = true;

		xSemaphoreGiveFromISR(load_manage_sem, pdFALSE);
	} else if (*temp == 4) {
		printf("un-volatility button pressed \n");
		load_volatility_state = false;

		xSemaphoreGiveFromISR(load_manage_sem, pdFALSE);
	}
}

void FrequencyAnalyzerInterrupt(void* context, alt_u32 id) {
	int* temp = (int*) context;

	// determine whether passed value is bad
	if (*temp == 1) {
		// check if timer active
		if (xTimerIsTimerActive(load_timer) != pdFALSE) {
			// If active reset timer
			BaseType_t xHigherPriorityTaskWoken = pdFALSE;

			xTimerResetFromISR(load_timer, &xHigherPriorityTaskWoken);
		} else {
			// call load_control task
			xSemaphoreGiveFromISR(load_manage_sem, pdFALSE);
		}
	}
}

int main(void) {
	// set up semaphores
	maintenance_sem = xSemaphoreCreateBinary();
	load_manage_sem = xSemaphoreCreateBinary();

	switchPollInit();
	maintanenceInit();
	int timer_id = 1;

	// set up timer
	load_timer = xTimerCreate("load Timer", pdMS_TO_TICKS(5000), pdFALSE, &timer_id, load_control_callback);

	// Tasks
	xTaskCreate( ToggleMaintenanceTask, "Maintenance Task", configMINIMAL_STACK_SIZE, &uiButtonValue, MAINTENANCE_TASK_PRIORITY, NULL);
	xTaskCreate( SwitchPollingTask, "Switch Polling Task",configMINIMAL_STACK_SIZE, &uiSwitchValue, SWITCH_POLLING_TASK_PRIORITY, NULL);
	xTaskCreate( LoadManagementTask, "load Manager Task", configMINIMAL_STACK_SIZE, NULL, LOAD_MANAGER_TASK_PRIORITY, NULL);

	/* Finally start the scheduler. */
	vTaskStartScheduler();

	/* Will only reach here if there is insufficient heap available to start
	 the scheduler. */
	for (;;)
		;
}

// timer call back
void load_control_callback(TimerHandle_t xTimer) {
	printf("load_control:: timer call back...\n");
	xSemaphoreGiveFromISR(load_manage_sem, pdFALSE);
}

void maintanenceInit() {
	// clears the edge capture register. Writing 1 to bit clears pending interrupt for corresponding button.
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);
	IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, 0x0);
	// enable interrupts for first two buttons (for now)
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE, 0x7);

	// register the ISR
	alt_irq_register(PUSH_BUTTON_IRQ, (void*) &uiButtonValue, button_interrupts_function);
}

void switchPollInit() {
	// switch polling setup
	IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, 0x0);
	IOWR_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE, 0x0);
}

static void LoadManagementTask(void *pvParameters) {
	while (1) {
		// wait for semaphore release
		if (xSemaphoreTake(load_manage_sem, ( TickType_t ) 10 )) {
			// if timer active reset
			if (xTimerIsTimerActive(load_timer) != pdFALSE) {
				printf("reseting timer as still active\n");
				xTimerReset(load_timer, 10);
			} else {
				// do no computation if not in load balancing state and input is not volatile
				if ((load_control_state == false) && (load_volatility_state == false)) {
					printf("no volatility and no load control!\n");

				} else {
					// TODO: Check if a switch has been turned off that was previously on
					// and update logic to ignore on shedding and un-shedding.


					// if input is volatile
					if (load_volatility_state == 1) {
						int load = 0;
						int pos = 0;
						int i;

						// find least significant bit in which both the blocked_load_mask and load_value are equal
						// This is the least important load to shed.
						for (i = 0; i < NUM_OF_LOADS; i++) {
							// if anding the blocked mask with the only bit is the bit (then active).
							pos = (int) pow(2, i);
							if ((blocked_load_mask & pos) == pos) {
								if ((load_value & pos) == pos) {
									load = pos;
									break;
								}
							}
						}

						// TODO: turn on green LED
						printf("removing load: %d\n", load);
						blocked_load_mask ^= load;

						xTimerReset(load_timer, 10);
					} else {
						// not volatile so add load
						if (blocked_load_mask != 31) {
							int load = 0;
							int i;

							for (i = NUM_OF_LOADS-1; i >= 0; i--) {
								// if anding the blocked mask with the only bit is 0 => found
								if ((blocked_load_mask & (int) pow(2, i)) == 0) {
									load = (int) pow(2, i);
									break;
								}
							}

							//TODO: turn off green LED

							printf("Turning on load: %d ", load);
							blocked_load_mask = blocked_load_mask | load;

							printf("reseting timer as not all LOADS are switched back on.\n");
							xTimerReset(load_timer, 10);
						} else {
							load_control_state = false;
							printf("Exiting load balancing state!\n");
						}
					}
					load_value = load_value & LOAD_MASK;
					blocked_load_mask = blocked_load_mask & LOAD_MASK;

					// set LEDS
					printf("%d ", blocked_load_mask);
					printf(" : %d : ", load_value);
					printf("%d\n", load_value & blocked_load_mask);
					IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, load_value & blocked_load_mask);
				}
			}
		}
	}
}

static void ToggleMaintenanceTask(void *pvParameters) {
	while (1) {
		if (xSemaphoreTake(maintenance_sem, ( TickType_t ) 10 )) {
			printf("Maintenance Task \n");

			//TODO: actually enter maintenance state and turn off load manager.

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

		if (load_control_state == false) {
			// write the value of the switches to the red LEDs
			IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, *temp & LOAD_MASK);

			load_value = *temp;
		} else {
			// TODO: only able to turn off loads
			// immediately turn off load and some how update load manager about said task
		}

		// delay for 100ms
		vTaskDelay(1000);
	}
}
