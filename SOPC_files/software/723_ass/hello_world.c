/*
 * "Hello World" example.
 *
 * This example prints 'Hello from Nios II' to the STDOUT stream. It runs on
 * the Nios II 'standard', 'full_featured', 'fast', and 'low_cost' example
 * designs. It runs with or without the MicroC/OS-II RTOS and requires a STDOUT
 * device in your system's hardware.
 * The memory footprint of this hosted application is ~69 kbytes by default
 * using the standard reference design.
 *
 * For a reduced footprint version of this template, and an explanation of how
 * to reduce the memory footprint for a given application, see the
 * "small_hello_world" template.
 *
 */

#include "FreeRTOS/queue.h"
#include <stdint.h>
#include <stdio.h>

/*
 * CONSTANT VARIABLES
 */
#define LOAD_MASK 31
#define NUM_OF_LOADS 5
#define SAMPLING_FREQUENCY 16000
#define INSTANTANEOUS_FREQUENCY_THRESHOLD 5
#define LOAD_MANAGEMENT_TIMER_INTERVAL 500

int loadManagementTimerId;
int vgaRefreshTimerId;
int switchPollTimerId;

struct LoadStatus {
  uint32_t activatedLoads;
  uint32_t blockedLoads;
};

static void maintenanceTask(void *pvParameters);
static void frequencyAnalyserTask(void *pvParameters);
static void loadManagerTask(void *pvParameters);
static void keyboardTask(void *pvParameters);
static void vgaRefreshTask(void *pvParameters);
static void ledManagerTask(void *pvParameters);
static void switchPollTask(void *pvParameters);

static void pushButtonISR();
static void frequencyDetectorISR();
static void keyboardISR();

TimerHandle_t loadManagementTimer;
TimerHandle_t vgaRefreshTimer;
TimerHandle_t switchPollTimer;

struct maintenanceState_t {
  SemaphoreHandle_t mutex;
  bool inMaintenance;
} maintenanceState;

struct frequencyHistoryState_t {
  SemaphoreHandle_t mutex;
  double freqHistory[100];
  double freqRocHistory[100];
  int i;
} frequencyHistoryState;

struct thresholdState_t {
  SemaphoreHandle_t mutex;
  double threshold;
} thresholdState;

struct blockedLoadState_t {
  SemaphoreHandle_t mutex;
  uint32_t blockedLoads;
} blockedLoadState;

struct activatedLoadState_t {
  SemaphoreHandle_t mutex;
  uint32_t activatedLoads;
} activatedLoadState;

struct loadManagementState_t {
  SemaphoreHandle_t mutex;
  bool isManagingLoads;
} loadManagementState;

struct stabilityState_t {
  SemaphoreHandle_t mutex;
  bool isStable;
} stabilityState;

SemaphoreHandle_t maintenanceSemaphore;
SemaphoreHandle_t keyboardSemaphore;
SemaphoreHandle_t frequencySemaphore;
SemaphoreHandle_t loadManagementSemaphore;

static QueueHandle_t loadControlQueue;
static QueueHandle_t frequencyQueue;

volatile int last_number_of_samples = 0;

void frequency_analyser_ISR() {
  unsigned int num_of_sample = IORD(FREQUENCY_ANALYSER_BASE, 0);

  xQueueSendToBackFromISR(Q_frequency, &num_of_sample, pdFALSE);
}

void setupSemaphores() {
  maintenanceSemaphore = xSemaphoreCreateBinary();
  keyboardSemaphore = xSemaphoreCreateBinary();
  frequencySemaphore = xSemaphoreCreateBinary();
  loadManagementSemaphore = xSemaphoreCreateBinary();
}

void setupTasks() {
  xTaskCreate(maintenanceTask, "Maintenance Task", configMINIMAL_STACK_SIZE,
              NULL, MAINTENANCE_TASK_PRIORITY, NULL);
  xTaskCreate(frequencyAnalyserTask, "Frequency Analyser Task",
              configMINIMAL_STACK_SIZE, NULL, FREQUENCY_TASK_PRIORITY, NULL);
  xTaskCreate(loadManagerTask, "Load Manager Task", configMINIMAL_STACK_SIZE,
              NULL, LOAD_MANAGER_TASK_PRIORITY, NULL);
  xTaskCreate(keyboardTask, "Keyboard Task", configMINIMAL_STACK_SIZE, NULL,
              KEYBOARD_TASK_PRIORITY, NULL);
  xTaskCreate(vgaRefreshTask, "VGA Display Task", configMINIMAL_STACK_SIZE,
              NULL, VGA_DISPLAY_TASK_PRIORITY, NULL);
  xTaskCreate(ledManagerTask, "LED Manager Task", configMINIMAL_STACK_SIZE,
              NULL, LED_MANAGER_TASK_PRIORITY, NULL);
  xTaskCreate(switchPollTask, "Switch Monitor Task", configMINIMAL_STACK_SIZE,
              NULL, SWITCH_MONITOR_TASK_PRIORITY, NULL);
}

void setupISRs() {
  alt_irq_register(PUSH_BUTTON_IRQ, NULL, pushButtonISR);
  alt_irq_register(FREQUENCY_ANALYSER_IRQ, NULL, frequencyDetectorISR);
  alt_irq_register(PS2_IRQ, NULL, keyboardISR);
}

void setupTimers() {
  loadManagementTimer =
      xTimerCreate("Load Management Timer", pdMS_TO_TICKS(500), pdFALSE,
                   &loadManagementTimerId, loadManagerTask);
  vgaRefreshTimer = xTimerCreate("load Timer", pdMS_TO_TICKS(1000), pdFALSE,
                                 &vgaRefreshTimerId, vgaRefreshTask);
  switchPollTimer = xTimerCreate("load Timer", pdMS_TO_TICKS(1000), pdFALSE,
                                 &switchPollTimerId, switchPollTask);
}

void setupStates() {
  maintenanceState.mutex = xSemaphoreCreateBinary();
  maintenanceState.inMaintenance = false;

  frequencyHistoryState.mutex = xSemaphoreCreateBinary();
  frequencyHistoryState.i = 99;

  thresholdState.mutex = xSemaphoreCreateBinary();
  thresholdState.threshold = 0;

  blockedLoadState.mutex = xSemaphoreCreateBinary();
  blockedLoadState.blockedLoads = 0;

  activatedLoadState.mutex = xSemaphoreCreateBinary();
  activatedLoadState.activatedLoads;

  loadManagementState.mutex = xSemaphoreCreateBinary();
  activatedLoadState.isManagingLoads;
}

void setupQueues() {
        loadControlQueue = xQueueCreate( ??? );
        frequencyQueue = xQueueCreate(100, sizeof(double));
}

static void maintenanceTask(void *pvParameters) {
  while (1) {
    if (xSemaphoreTake(maintenanceSemaphore, (TickType_t)10)) {
      printf("Maintenance Task\n");

      if (xTimerIsTimerActive(loadManagementTimer)) {
        xTimerStop(loadManagementTimer, 10);
      }

      xSemaphoreTake(maintenanceState.mutex, portMAX_DELAY);
      xSemaphoreTake(loadManagementState.mutex, portMAX_DELAY);
      xSemaphoreTake(blockedLoadState.mutex, portMAX_DELAY);

      maintenanceState = !maintenanceState;
      loadManagementState.isManagingLoads = false;
      blockedLoadState.blockedLoads = false;

      xSemaphoreGive(loadManagementState.mutex);
      xSemaphoreGive(maintenanceState.mutex);
      xSemaphoreGive(blockedLoadState.mutex);
    }
  }
}

static void frequencyAnalyserTask(void *pvParameters) {
  while (1) {
    printf("reading from queue\n");
    // receive frequency data from queue
    while (uxQueueMessagesWaiting(frequencyQueue) != 0) {
      xSemaphoreTake(frequencyHistoryState.mutex, portMAX_DELAY);

      xQueueReceive(
          frequencyQueue,
          frequencyHistoryState.frequencyHistory + frequencyHistoryState.i, 0);

      if (frequencyHistoryState.i == 0) {
        frequencyHistoryState.freqRocHistory[0] =
            (frequencyHistoryState.freqHistory[0] -
             frequencyHistoryState.freqHistory[99]) *
            2.0 * frequencyHistoryState.freqHistory[0] *
            frequencyHistoryState.freqHistory[99] /
            (frequencyHistoryState.freqHistory[0] +
             frequencyHistoryState.freqHistory[99]);
      } else {
        frequencyHistoryState.freqRocHistory[frequencyHistoryState.i] =
            (frequencyHistoryState.freqHistory[frequencyHistoryState.i] -
             frequencyHistoryState.freqHistory[frequencyHistoryState.i - 1]) *
            2.0 * frequencyHistoryState.freqHistory[frequencyHistoryState.i] *
            frequencyHistoryState.freqHistory[frequencyHistoryState.i - 1] /
            (frequencyHistoryState.freqHistory[frequencyHistoryState.i] +
             frequencyHistoryState.freqHistory[frequencyHistoryState.i - 1]);
      }

      if (frequencyHistoryState.freqRocHistory[frequencyHistoryState.i] >
          100.0) {
        frequencyHistoryState.freqRocHistory[frequencyHistoryState.i] = 100.0;
      }

      frequencyHistoryState.i = ++frequencyHistoryState.i %
                                100; // point to the next data (oldest) to be

      xSemaphoreTake(thresholdState.mutex, portMAX_DELAY);

      bool isStable =
          (frequencyHistoryState.freqHistory[frequencyHistoryState.i] >
           INSTANTANEOUS_FREQUENCY_THRESHOLD) ||
          (abs(frequencyHistoryState.freqRocHistory[frequencyHistoryState.i]) >
           thresholdState.threshold);

      xSemaphoreGive(thresholdState.mutex);

      xSemaphoreTake(stabilityState.mutex, portMAX_DELAY);
      xSemaphoreTake(maintenanceState.mutex, portMAX_DELAY);

      bool callLoadManager = (isStable != stabilityState.isStable &&
                              !maintenanceState.inMaintenance);
      stabilityState.isStable = isStable;

      xSemaphoreGive(maintenanceState.mutex);
      xSemaphoreGive(stabilityState.mutex);

      xSemaphoreGive(frequencyHistoryState.mutex);

      if (callLoadManager) {
        xSemaphoreGive(loadManagementSemaphore);
      }
    }
  }
}

/**
 *  Find least significant bit in which both the blocked_load_mask and
 * load_value are equal. This is the least important load to shed.
 */
static void turn_off_least_important_load() {
  int pos = 0;
  int i;

  for (i = 0; i < NUM_OF_LOADS; i++) {
    // if anding the blocked mask with the only bit is the bit (then active).
    pos = (int)pow(2, i);
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
    pos = (int)pow(2, i);
    if ((blocked_loads & pos) == pos) {
      blocked_loads -= pos;

      // if adding this load did not turn off all load blocking then reset
      // timer.
      if (blocked_loads != 0) {
        printf("reseting timer as not all LOADS are switched back on.\n");
        xTimerReset(load_timer, 10);
      }

      printf("Turning on load: %d ", pos);
      break;
    }
  }
}

static void loadManagerTask(void *pvParameters) {
  while (1) {
    if (xSemaphoreTake(loadManagementSemaphore, (TickType_t)10)) {
      // if timer is active, reset and do no computation
      if (xTimerIsTimerActive(loadManagementTimer) != pdFALSE) {
        printf("reseting timer as already active\n");
        xTimerReset(loadManagementTimer, 10);
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

static void keyboardTask(void *pvParameters) {}

static void vgaRefreshTask(void *pvParameters) {}

static void ledManagerTask(void *pvParameters) {
  struct LoadStatus loads;
  while (1) {
    if (xQueueReceive(loadControlQueue, &loads, portMAX_DELAY) == pdTRUE) {
      IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, loads.activatedLoads);
      IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, loads.blockedLoads);
    }
  }
}

static void switchPollTask(void *pvParameters) {
  while (1) {
    unsigned int switchValue = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE);
    switchValue &= LOAD_MASK;

    struct LoadStatus loads;
    xSemaphoreTake(loadManagementState.mutex, portMAX_DELAY);
    if (!loadManagement.isManagingLoads) {
      loads.activatedLoads = switchValue;
      loads.blockedLoads = 0;
    } else {
      // only allow loads to be turned off and not on
      loads.blockedLoads &= *switchValue;
      loads.activatedLoads &= *switchValue;

      xSemaphoreTake(activatedLoadsState.mutex, portMAX_DELAY);
      xSemaphoreTake(blockedLoadsState.mutex, portMAX_DELAY);

      loads.activatedLoads =
          activatedLoadsState.activatedLoads - blockedLoadsState.blockedLoads;
      loads.blockedLoads = blockedLoadsState.blockedLoads;

      xSemaphoreGive(activatedLoadsState.mutex);
      xSemaphoreGive(blockedLoadsState.mutex);
    }
    xQueueSendToBack(loadControlQueue, &loads, pdFALSE);
    xSemaphoreGive(loadManagementState.mutex);

    vTaskDelay(100);
  }
}

static void pushButtonISR() {
  // need to cast the context first before using it
  int *temp = (int *)context;
  (*temp) = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE);

  // clears the edge capture register
  IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);

  // This logic is in place of actual relay for now.
  if (*temp == 1) {
    xSemaphoreGiveFromISR(maintenance_sem, pdFALSE);
  }
}

static void frequencyDetectorISR() {
  unsigned int numberOfSamples = IORD(FREQUENCY_ANALYSER_BASE, 0);
  int signalFrequency = SAMPLING_FREQUENCY / numberOfSamples;
  xQueueSendToBackFromISR(frequencyQueue, &signalFrequency, pdFALSE);
}

static void keyboardISR() {}

int main() {
  setupSemaphores();
  setupTasks();
  setupISRs();
  setupTimers();
  setupStates();
  setupQueues();
  vTaskStartScheduler();

  while (true)
    ;
  return 0;
}

// typedef struct{
//	unsigned int x1;
//	unsigned int y1;
//	unsigned int x2;
//	unsigned int y2;
//}Line;

// void PRVGADraw_Task(void *pvParameters ){
//
//
//	//initialize VGA controllers
//	alt_up_pixel_buffer_dma_dev *pixel_buf;
//	pixel_buf =
// alt_up_pixel_buffer_dma_open_dev(VIDEO_PIXEL_BUFFER_DMA_NAME);
// if(pixel_buf == NULL){ 		printf("can't find pixel buffer
// device\n");
//	}
//	alt_up_pixel_buffer_dma_clear_screen(pixel_buf, 0);
//
//	alt_up_char_buffer_dev *char_buf;
//	char_buf =
// alt_up_char_buffer_open_dev("/dev/video_character_buffer_with_dma");
//	if(char_buf == NULL){
//		printf("can't find char buffer device\n");
//	}
//	alt_up_char_buffer_clear(char_buf);
//
//
//
//	//Set up plot axes
//	alt_up_pixel_buffer_dma_draw_hline(pixel_buf, 100, 590, 200, ((0x3ff <<
// 20) + (0x3ff << 10) + (0x3ff)), 0);
//	alt_up_pixel_buffer_dma_draw_hline(pixel_buf, 100, 590, 300, ((0x3ff <<
// 20) + (0x3ff << 10) + (0x3ff)), 0);
//	alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, 50, 200, ((0x3ff <<
// 20) + (0x3ff << 10) + (0x3ff)), 0);
//	alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, 220, 300, ((0x3ff <<
// 20) + (0x3ff << 10) + (0x3ff)), 0);
//
//	alt_up_char_buffer_string(char_buf, "Frequency(Hz)", 4, 4);
//	alt_up_char_buffer_string(char_buf, "52", 10, 7);
//	alt_up_char_buffer_string(char_buf, "50", 10, 12);
//	alt_up_char_buffer_string(char_buf, "48", 10, 17);
//	alt_up_char_buffer_string(char_buf, "46", 10, 22);
//
//	alt_up_char_buffer_string(char_buf, "df/dt(Hz/s)", 4, 26);
//	alt_up_char_buffer_string(char_buf, "60", 10, 28);
//	alt_up_char_buffer_string(char_buf, "30", 10, 30);
//	alt_up_char_buffer_string(char_buf, "0", 10, 32);
//	alt_up_char_buffer_string(char_buf, "-30", 9, 34);
//	alt_up_char_buffer_string(char_buf, "-60", 9, 36);
//
//	double freq[100], dfreq[100];
//	int i = 99, j = 0;
//	Line line_freq, line_roc;
//
//	while(1){
//		printf("reading from queue\n");
//		//receive frequency data from queue
//		while(uxQueueMessagesWaiting( Q_freq_data ) != 0){
//			xQueueReceive( Q_freq_data, freq+i, 0 );
//
//			//calculate frequency RoC
//
//			if(i==0){
//				dfreq[0] = (freq[0]-freq[99]) * 2.0 * freq[0] *
// freq[99] / (freq[0]+freq[99]);
//			}
//			else{
//				dfreq[i] = (freq[i]-freq[i-1]) * 2.0 * freq[i]*
// freq[i-1] / (freq[i]+freq[i-1]);
//			}
//
//			if (dfreq[i] > 100.0){
//				dfreq[i] = 100.0;
//			}
//
//
//			i =	++i%100; //point to the next data (oldest) to be
// overwritten
//
//		}
//		printf("printing to screen \n");
//		//clear old graph to draw new graph
//		alt_up_pixel_buffer_dma_draw_box(pixel_buf, 101, 0, 639, 199, 0,
// 0); 		alt_up_pixel_buffer_dma_draw_box(pixel_buf, 101, 201, 639, 299,
// 0, 0);
//
//		// logic added by ben that needs to be dynamically updated
//
//		alt_up_char_buffer_string(char_buf, "Lower threshold:", 9, 40);
//		alt_up_char_buffer_string(char_buf, "43.7 Hz", 28, 40);
//
//		alt_up_char_buffer_string(char_buf, "RoC threshold:", 9, 42);
//		alt_up_char_buffer_string(char_buf, "0.6 Hz/sec", 28, 42);
//
//		alt_up_char_buffer_string(char_buf, "System status", 50, 40);
//		alt_up_char_buffer_string(char_buf, "Stable", 54, 42);
//
//
//
//		for(j=0;j<99;++j){ //i here points to the oldest data, j loops
// through all the data to be drawn on VGA 			if
// (((int)(freq[(i+j)%100]) > MIN_FREQ) && ((int)(freq[(i+j+1)%100]) >
// MIN_FREQ)){
//				//Calculate coordinates of the two data points
// to draw a line in between
//				//Frequency plot
//				line_freq.x1 = FREQPLT_ORI_X +
// FREQPLT_GRID_SIZE_X
//*
// j; 				line_freq.y1 = (int)(FREQPLT_ORI_Y -
// FREQPLT_FREQ_RES
// * (freq[(i+j)%100] - MIN_FREQ));
//
//				line_freq.x2 = FREQPLT_ORI_X +
// FREQPLT_GRID_SIZE_X
//* (j + 1); 				line_freq.y2 = (int)(FREQPLT_ORI_Y -
// FREQPLT_FREQ_RES * (freq[(i+j+1)%100] - MIN_FREQ));
//
//				//Frequency RoC plot
//				line_roc.x1 = ROCPLT_ORI_X + ROCPLT_GRID_SIZE_X
//*
// j; 				line_roc.y1 = (int)(ROCPLT_ORI_Y -
// ROCPLT_ROC_RES
// * dfreq[(i+j)%100]);
//
//				line_roc.x2 = ROCPLT_ORI_X + ROCPLT_GRID_SIZE_X
//* (j
//+
// 1); 				line_roc.y2 = (int)(ROCPLT_ORI_Y -
// ROCPLT_ROC_RES
// * dfreq[(i+j+1)%100]);
//
//				//Draw
//				alt_up_pixel_buffer_dma_draw_line(pixel_buf,
// line_freq.x1, line_freq.y1, line_freq.x2, line_freq.y2, 0x3ff << 0, 0);
//				alt_up_pixel_buffer_dma_draw_line(pixel_buf,
// line_roc.x1, line_roc.y1, line_roc.x2, line_roc.y2, 0x3ff << 0, 0);
//			}
//		}
//		vTaskDelay(10);
//
//	}
//}
