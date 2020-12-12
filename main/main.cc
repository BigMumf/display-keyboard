/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#include <esp_log.h>
#include <esp_spi_flash.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include "display.h"
#include "usb.h"

namespace {

constexpr char TAG[] = "main";
constexpr uint64_t kMaxMainLoopWaitUsecs = 50000;

void WaitForDebugMonitor() {
  // Poor man's way of waiting till the monitor has connected.
  const TickType_t kStartupDelay = 1000 / portTICK_PERIOD_MS;
  ESP_LOGI(TAG, "Waiting for debug monitor connection");
  vTaskDelay(kStartupDelay);
}

}  // namespace

// Current breadboard wiring for Cucumber board:
// MISO => 13 # don't need this (display is read-only)
// SCK  => 12
// MOSI => 11
// CS   => 38
// DC   => 34
// RST  => 37

extern "C" void app_main(void) {
  WaitForDebugMonitor();

  USB usb;
  Display display(320, 240);
  display.Initialize();

  printf("Keyboard app!\n");

  /* Print chip information */
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  printf("This is %s chip with %d CPU core(s), WiFi%s%s, ", CONFIG_IDF_TARGET,
         chip_info.cores, (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
         (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

  printf("silicon revision %d, ", chip_info.revision);

  printf(
      "%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
      (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

  printf("Minimum free heap size: %d bytes\n",
         esp_get_minimum_free_heap_size());

  printf("Going into loop.\n");
  fflush(stdout);
  display.Update();
  while (true) {
    uint32_t wait_usecs = display.HandleTask();
    if (!wait_usecs)
      wait_usecs = 5000;  // 5 msec.
    else if (wait_usecs > kMaxMainLoopWaitUsecs)
      wait_usecs = kMaxMainLoopWaitUsecs;
    ets_delay_us(wait_usecs);
    taskYIELD();
  }
}