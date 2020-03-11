#include "wifi_creds.h"
#include "ssd1306.h"
#include "qrcodegen.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp32/rom/ets_sys.h>

#include <esp_system.h>
#include <esp_spi_flash.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_log.h>

#include <nvs_flash.h>

#include <driver/i2c.h>

#include <stdio.h>
#include <string.h>

static const char *TAG = "simple wifi";
extern const uint8_t something_dat_start[] asm("_binary_something_dat_start");
extern const uint8_t something_dat_end[] asm("_binary_something_dat_end");

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about one event - are we connected to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;

static esp_err_t
event_handler(void *ctx, system_event_t *event) {
    /* For accessing reason codes in case of disconnection */
    system_event_info_t *info = &event->event_info;
    
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START: {
            esp_wifi_connect();
            break;
        }

        case SYSTEM_EVENT_STA_GOT_IP: {
            ESP_LOGI(TAG, "got ip:%s",
                ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
            break;
        }

        case SYSTEM_EVENT_AP_STACONNECTED: {
            ESP_LOGI(TAG, "station:"MACSTR" join, AID=%d",
                MAC2STR(event->event_info.sta_connected.mac),
                event->event_info.sta_connected.aid);
            break;
        }

        case SYSTEM_EVENT_AP_STADISCONNECTED: {
            ESP_LOGI(TAG, "station:"MACSTR"leave, AID=%d",
                MAC2STR(event->event_info.sta_disconnected.mac),
                event->event_info.sta_disconnected.aid);
            break;
        }

        case SYSTEM_EVENT_STA_DISCONNECTED: {
            ESP_LOGE(TAG, "Disconnect reason : %d", info->disconnected.reason);
            if (info->disconnected.reason == WIFI_REASON_CONNECTION_FAIL) {
            /*Switch to 802.11 bgn mode */
            esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
            }
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            break;
        }

        default:
            break;
    }
    return ESP_OK;
}

void
wifi_init_sta() {
    wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = MY_SSID,
            .password = MY_PASSWORD
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

#define AP_SSID     "yadda"
#define AP_PASSWORD "qwerasdfzxcv"

void
wifi_init_ap() {
    wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASSWORD,
            .max_connection = 1,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_ap finished.");
}

// QR-Code versions, sizes and information capacity: https://www.qrcode.com/en/about/version.html
// Version 3 = 29 x 29, binary info cap by ecc mode: L:53, M:42, Q:32 bytes
#define WIFI_QR_VERSION 3
#define WIFI_QR_SIZE (qrcodegen_BUFFER_LEN_FOR_VERSION(WIFI_QR_VERSION))

void
app_main()
{
    printf("Hello world!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP8266 chip with %d CPU cores, WiFi, silicon revision %d, %dMB %s flash\n",
        chip_info.cores, chip_info.revision, spi_flash_get_chip_size() / (1024 * 1024),
        (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ssd1306_init(I2C_NUM_1, 23, 22);

    //wifi_init_sta();
    wifi_init_ap();


    uint8_t qrcode[WIFI_QR_SIZE];
    {
        uint8_t tempBuffer[WIFI_QR_SIZE];
        //
        // Encoding wifi parameters on QR-Code: https://github.com/zxing/zxing/wiki/Barcode-Contents#wi-fi-network-config-android-ios-11
        size_t input_length = snprintf((char*)tempBuffer, WIFI_QR_SIZE, "WIFI:S:%s:T:WPA;P:%s;;", AP_SSID, AP_PASSWORD);
        bool qr_status = qrcodegen_encodeBinary(tempBuffer, input_length, qrcode, qrcodegen_Ecc_LOW, WIFI_QR_VERSION, WIFI_QR_VERSION, qrcodegen_Mask_AUTO, true);
        printf("QR code generation done; status=%d\n", qr_status);
    }

    printf("QR code buffer; size=%d\n", WIFI_QR_SIZE);
    uint8_t *p = qrcode;
    for (int i = 0; i < WIFI_QR_SIZE; i += 0x10) {
        printf("%04x:", i);
        for (int j = 0; (j < 0x10) && ((i+j) < WIFI_QR_SIZE); ++j) {
            printf(" %02x", *p);
            ++p;
        }
        printf("\n");
    }

    ssd1306_set_range(I2C_NUM_1, 0, 28, 0, 3);
    ssd1306_send_data(I2C_NUM_1, qrcode, WIFI_QR_SIZE);


    /*for (int i = 10; i >= 0; i--) {
    printf("Restarting in %d seconds...\n", i);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart(); */
}

// vim: set sw=4 ts=4 indk= et:
