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

///* DE2-115 includes. */
#include "altera_avalon_pio_regs.h"	// to use PIO functions
#include "sys\alt_irq.h"  	// to register interrupts
/*
 * CONSTANT VARIABLES
 */
#define LOAD_MASK		31
#define NUM_OF_LOADS	5

// Task priorities
#define MAINTENANCE_TASK_PRIORITY		7
#define SWITCH_POLLING_TASK_PRIORITY	5
#define LOAD_MANAGER_TASK_PRIORITY		8
#define FREQ_ANALAYZER_TASK_PRIORITY 	10

/**
 * Function Definitions
 */

// Task definitions
static void ToggleMaintenanceTask(void *pvParameters);
static void LoadManagementTask(void *pvParameters);
static void SwitchPollingTask(void *pvParameters);
static void FrequencyTaskAnalyzer(void *pvParameters);
static void LoadControlTask(void *pvParameters);


// Interrupt Service Routine definitions
void MaintenanceButtonInterrupt(void* context, alt_u32 id);
//void FrequencyAnalyzerInterrupt(void* context, alt_u32 id);

// Timer call-backs definitions
void LoadControlTimerCallback(TimerHandle_t xTimer);

// Initialisation functions definitions
void switchPollInit();
void maintanenceInit();

/**
 * Global Variables
 */

// input global variables
unsigned int uiSwitchValue = 0;
unsigned int uiButtonValue = 0;

// System state global variables.
bool unstable_state = false;
bool maintenance_state = false;
bool managing_load_state = false;

// load management masks
struct loadValues {
	int loads;
	int blocked_loads;
};

int load_value = 0;
int blocked_load_mask = 31; // 31 -> block no LOADS (11111xb)
int blocked_loads = 0;

// frequency global variables
int rate_of_change_frequency = 0;
int current_frequency;
int frequency_history[5];

// global timer
TimerHandle_t load_timer;

static QueueHandle_t TaskQ;

// semaphores
SemaphoreHandle_t maintenance_sem;
SemaphoreHandle_t load_manage_sem;
SemaphoreHandle_t freq_sem;



/**
 * Functions
 */




int main(void) {
	// Set up semaphores
	maintenance_sem = xSemaphoreCreateBinary();
	load_manage_sem = xSemaphoreCreateBinary();
	freq_sem = xSemaphoreCreateBinary();

	// Set up timer
	int timer_id = 1;
	load_timer = xTimerCreate("load Timer", pdMS_TO_TICKS(1000), pdFALSE, &timer_id, LoadControlTimerCallback);

	switchPollInit();
	maintanenceInit();

	// Register the ISRs
	alt_irq_register(PUSH_BUTTON_IRQ, (void*) &uiButtonValue, MaintenanceButtonInterrupt);

	// Create Tasks
	xTaskCreate( ToggleMaintenanceTask, "Maintenance Task", configMINIMAL_STACK_SIZE, &uiButtonValue, MAINTENANCE_TASK_PRIORITY, NULL);
	xTaskCreate( SwitchPollingTask, "Switch Polling Task",configMINIMAL_STACK_SIZE, &uiSwitchValue, SWITCH_POLLING_TASK_PRIORITY, NULL);
	xTaskCreate( LoadManagementTask, "load Manager Task", configMINIMAL_STACK_SIZE, NULL, LOAD_MANAGER_TASK_PRIORITY, NULL);
	xTaskCreate( FrequencyTaskAnalyzer, "Frequency anal Task", configMINIMAL_STACK_SIZE, NULL, FREQ_ANALAYZER_TASK_PRIORITY, NULL);

	/* Finally start the scheduler. */
	vTaskStartScheduler();

	/* Will only reach here if there is insufficient heap available to start
	 the scheduler. */
	for (;;)
		;
}

// TODO: A queue for messaging to load controller.
// TODO: A task responsible for controlling loads.
// TODO: A task that analyzes frequencies and starts load maintenance state
// TODO: A task that displays VGA information
// TODO: A keyboard IRQ
static void FrequencyTaskAnalyzer(void *pvParameters) {
	while (1) {
		if (xSemaphoreTake(freq_sem, 10)) {
			printf("Relay is volatile \n");
			load_volatility_state = true;
			load_control_state = true;

			xSemaphoreGive(load_manage_sem);
		}
	}

}

void MaintenanceButtonInterrupt(void* context, alt_u32 id) {
	// need to cast the context first before using it
	int* temp = (int*) context;
	(*temp) = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE);

	// clears the edge capture register
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);

	// This logic is in place of actual relay for now.
	if (*temp == 1) {
		xSemaphoreGiveFromISR(maintenance_sem, pdFALSE);
	}
}

// currently not used
//void FrequencyAnalyzerInterrupt(void* context, alt_u32 id) {
//	int* temp = (int*) context;
//
//	// determine whether passed value is bad
//	if (*temp == 1) {
//		// check if timer active
//		if (xTimerIsTimerActive(load_timer) != pdFALSE) {
//			// If active reset timer
//			BaseType_t xHigherPriorityTaskWoken = pdFALSE;
//
//			xTimerResetFromISR(load_timer, &xHigherPriorityTaskWoken);
//		} else {
//			// call load_control task
//			xSemaphoreGiveFromISR(load_manage_sem, pdFALSE);
//		}
//	}
//}

void LoadControlTimerCallback(TimerHandle_t xTimer) {
	printf("timer call back...\n");
	xSemaphoreGiveFromISR(load_manage_sem, pdFALSE);
}

void maintanenceInit() {
	// clears the edge capture register. Writing 1 to bit clears pending interrupt for corresponding button.
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);
	IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, 0x0);
	// enable interrupts for first two buttons (for now)
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE, 0x7);
}

void switchPollInit() {
	// switch polling setup
	IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, 0x0);
	IOWR_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE, 0x0);
}


/**
 *  Find least significant bit in which both the blocked_load_mask and load_value are equal.
 * 	This is the least important load to shed.
*/
static void turn_off_least_important_load() {
	int pos = 0;
	int i;

	for (i = 0; i < NUM_OF_LOADS; i++) {
		// if anding the blocked mask with the only bit is the bit (then active).
		pos = (int) pow(2, i);
		if ((blocked_loads & pos) == 0) {
			if ((load_value & pos) == pos) {
				blocked_loads += pos;

				printf("removing load: %d\n", pos);
				return;
			}
		}
	}
}

/**
 *	 Find most important load to turn on inside of the shed loads.
 */
static void turn_on_most_important_load() {
	int pos = 0;
	int i;

	for (i = NUM_OF_LOADS - 1; i >= 0; i--) {
		// if anding the blocked mask with the only bit is 0 => found
		pos = (int) pow(2, i);
		if ((blocked_loads & pos) == pos) {
			blocked_loads -= pos;

			// if adding this load did not turn off all load blocking then reset timer.
			if (blocked_loads != 0) {
				printf("reseting timer as not all LOADS are switched back on.\n");
				xTimerReset(load_timer, 10);
			}

			printf("Turning on load: %d ", pos);
			break;
		}
	}
}

static void LoadManagementTask(void *pvParameters) {
	while (1) {
		// wait for semaphore release
		if (xSemaphoreTake(load_manage_sem, ( TickType_t ) 10 )) {
			// if timer is active, reset and do no computation
			if (xTimerIsTimerActive(load_timer) != pdFALSE) {
				printf("reseting timer as already active\n");
				xTimerReset(load_timer, 10);
			} else {
				if (unstable_state) {
					managing_load_state = true;
					turn_off_least_important_load();

					printf("reseting timer as state is unstable\n");
					xTimerReset(load_timer, 10);
				} else if (!unstable_state && managing_load_state) {
					turn_on_most_important_load();

					// reset timer if more loads to unblock, else exit control state
					if (blocked_loads > 0) {
						printf("reseting timer as more loads need reconnecting\n");
						xTimerReset(load_timer, 10);
					} else {
						printf("exiting load management state\n");
						managing_load_state = false;
					}
				}

				struct loadValues loads;
				loads.loads = load_value - blocked_loads;
				loads.blocked_loads = blocked_loads;

				xQueueSendToBack(TaskQ, loads, pdFALSE);
			}
		}
	}
}

static void ToggleMaintenanceTask(void *pvParameters) {
	while (1) {
		if (xSemaphoreTake(maintenance_sem, ( TickType_t ) 10 )) {
			printf("Maintenance Task \n");

			// turn off load manager timer if active
			if (xTimerIsTimerActive(load_timer)) {
				xTimerStop(load_timer, 10);
			}

			// toggle maintenance state and set managing loads to false
			maintenance_state = !maintenance_state;
			managing_load_state = false;

			// remove all blocked loads
			blocked_loads = 0;
		}
	}
}

static void SwitchPollingTask(void *pvParameters) {
	while (1) {
		unsigned int* temp = (unsigned int*) pvParameters;

		// read the value of the switches
		*temp = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE);
		*temp &= LOAD_MASK;

		struct loadValues loads;

		if (managing_load_state == false) {
			// TODO: could be a queue -> to turn on "loads"
			// send led val to queue
			loads.loads = *temp;
			loads.blocked_loads = 0;
		} else {
			// only allow loads to be turned off and not on
			blocked_loads &= *temp;
			load_value &= *temp;

			loads.loads = load_value - blocked_loads;
			loads.blocked_loads = blocked_loads;
		}
		xQueueSendToBack(TaskQ, &loads, pdFALSE);

		// delay for 100ms
		vTaskDelay(100);
	}
}

static void LoadControlTask(void *pvParameters) {
	struct loadValues loads;
	while (1) {
		if(xQueueReceive( TaskQ, &loads, portMAX_DELAY ) == pdTRUE ) {
			IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, loads.loads);
			IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, loads.blocked_loads);
		}
	}
}


