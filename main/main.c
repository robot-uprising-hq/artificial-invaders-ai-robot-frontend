/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "nvs_flash.h"
#include <ctype.h>
#include "esp_wifi.h"
#include "console.h"
#include "wifiscan.h"
#include "wificonnect.h"

static const char* TAG = "robotfrontend";

/*
 * Uninstall UART communications driver.
 */
static void uart0_deinit(void)
{
    uart_driver_delete(UART_NUM_0);
}

/*
 * Install UART communications driver.
 */
static void uart0_init(void)
{
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    esp_vfs_dev_uart_use_driver(0);
}

/*
 * Right-trim whitespace from given buffer.
 */
static void rtrim(char *buf, size_t buf_len)
{
    for (int ii = 0; ii < buf_len; ii++) {
        char *p = &buf[buf_len - 1 - ii];
        if (isspace(*p)) {
            *p = '\0';
        } else if (*p == '\0') {
            continue;
        } else {
            // Anything else, stop trim.
            break;
        }
    }
}

static esp_err_t set_config(nvs_handle_t handle, const char *key, const char *val)
{
    esp_err_t err = nvs_set_str(handle, key, val);
    if (err != ESP_OK) {
        printf("Failed to write key: %s\n", esp_err_to_name(err));
    } else {
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            printf("Commit failed after writing key: %s\n", esp_err_to_name(err));
        }
        printf("Wrote %s.", key);
    }
    return err;
}

static void command_loop_task(void *param)
{
    uart0_init();

    esp_err_t err;
    nvs_handle_t handle = 0;
    err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open storage: %s",  esp_err_to_name(err));
    }

    while (1)
    {
        char ipaddr[32] = "";

        get_ip_addr(ipaddr, sizeof(ipaddr));
        printf("%s%sEnter command:\n", ipaddr, *ipaddr ? " " : "");
        char buf[60] = "";
        my_fgets(buf, sizeof(buf) - 1, fileno(stdin));
        buf[sizeof(buf) - 1] = '\0';
        printf("\nYou wrote: %s\n", buf);

        if (strncmp(buf, "scan", 4) == 0) {
            wifi_scan();
        } else if (strncmp(buf, "query", 5) == 0) {
            char string[40] = "";
            size_t string_length = sizeof(string);
            err = nvs_get_str(handle, "wifi-ssid", string, &string_length);
            if (err != ESP_OK) {
                printf("Failed to get key: %s\n", esp_err_to_name(err));
            } else {
                printf("wifi-ssid: \"%s\"\n", string);
            }

            string_length = sizeof(string);
            err = nvs_get_str(handle, "wifi-passwd", string, &string_length);
            if (err != ESP_OK) {
                printf("Failed to get key: %s\n", esp_err_to_name(err));
            } else {
                if (strncmp(buf, "query all", 9) == 0) {
                    printf("wifi-passwd: \"%s\"\n", string);
                } else {
                    printf("wifi-passwd: <is set>\n");
                }
            }
        } else if (strncmp(buf, "set ssid", 8) == 0) {
            rtrim(buf, sizeof(buf));
            set_config(handle, "wifi-ssid", &buf[9]);
        } else if (strncmp(buf, "set passwd", 10) == 0) {
            rtrim(buf, sizeof(buf));
            set_config(handle, "wifi-passwd", &buf[11]);
        } else if (strncmp(buf, "wifistart", 9) == 0) {
            char passwd[60] = "";
            size_t passwd_length = sizeof(passwd);
            char ssid[32] = "";
            size_t ssid_length = sizeof(ssid);

            err = nvs_get_str(handle, "wifi-passwd", passwd, &passwd_length);
            if (err == ESP_OK) {
                err = nvs_get_str(handle, "wifi-ssid", ssid, &ssid_length);
                if (err == ESP_OK) {
                    printf("Attempting Wifi connection to %s...\n", ssid);
                    wifi_init_sta(ssid, passwd);
                }
            }
            if (err != ESP_OK) {
                printf("Wifi not configured.\n");
            }
        } else if (strncmp(buf, "wifistop", 8) == 0) {

            err = esp_wifi_stop();
            if (err != ESP_OK) {
                printf("Failed to stop wifi: %s\n", esp_err_to_name(err));
            }
        } else if (strncmp(buf, "quit", 4) == 0) {
            printf("Quitting for testing deinit code...\n");
            break;
        }
    }

    nvs_close(handle);
    uart0_deinit();
    vTaskDelete(NULL);
}

void app_main(void)
{
    printf("*** Robot Uprising: Micro-Invaders ***\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU cores, WiFi%s%s, ",
            CONFIG_IDF_TARGET,
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Free heap: %d\n", esp_get_free_heap_size());

    // Needed for wifi code.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    xTaskCreate(command_loop_task, "command_loop", 4*1024, NULL, 5, NULL);
}
