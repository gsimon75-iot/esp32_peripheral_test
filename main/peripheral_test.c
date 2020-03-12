#include "wifi_creds.h"
#include "ssd1306.h"
#include "qrcodegen.h"
#include "font6x8.h"

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

#define AP_SSID     "yadda"
#define AP_PASSWORD "qwerasdfzxcv"

// QR-Code versions, sizes and information capacity: https://www.qrcode.com/en/about/version.html
// Version 3 = 29 x 29, binary info cap by ecc mode: L:53, M:42, Q:32 bytes
#define WIFI_QR_VERSION 3
#define WIFI_QR_SIZE (qrcodegen_BUFFER_LEN_FOR_VERSION(WIFI_QR_VERSION))

void
lcd_putchar(int col, int row, char c) {
    if ((c < 0x20) || (c & 0x80)) {
        c = 0x20;
    }
    col *= 6;
    ssd1306_set_range(I2C_NUM_1, col, col + 5, row, row);
    ssd1306_send_data(I2C_NUM_1, &font6x8[6 * (c - 0x20)], 6);
}

void
lcd_puts(int col, int row, const char *s) {
    if (!*s) {
        return;
    }
    col *= 6;
    ssd1306_set_range(I2C_NUM_1, col, 127, row, row);
    static uint8_t send_data_cmd[] = {
        0x78, 0x40,
    };
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write(cmd, send_data_cmd, sizeof(send_data_cmd), true);
    while (*s) {
        char c = *s;
        if ((c < 0x20) || (c & 0x80)) {
            i2c_master_write(cmd, (uint8_t*)&font6x8[0], 6, true);
        }
        else {
            i2c_master_write(cmd, (uint8_t*)&font6x8[6 * (c - 0x20)], 6, true);
        }
        ++s;
    }
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_NUM_1, cmd, 1000);
    i2c_cmd_link_delete(cmd);
}

void
display_ap_info(void) {

    uint8_t qrcode[WIFI_QR_SIZE];
    {
        uint8_t tempBuffer[WIFI_QR_SIZE];
        // Encoding wifi parameters on QR-Code: https://github.com/zxing/zxing/wiki/Barcode-Contents#wi-fi-network-config-android-ios-11
        size_t input_length = snprintf((char*)tempBuffer, WIFI_QR_SIZE, "WIFI:S:%s;T:WPA;P:%s;;", AP_SSID, AP_PASSWORD);
        bool qr_status = qrcodegen_encodeBinary(tempBuffer, input_length, qrcode, qrcodegen_Ecc_LOW, WIFI_QR_VERSION, WIFI_QR_VERSION, qrcodegen_Mask_AUTO, true);
        ESP_LOGI(TAG, "QR code generation done; status=%d, size=%d\n", qr_status, WIFI_QR_SIZE);
    }
    ESP_LOG_BUFFER_HEXDUMP(TAG, qrcode, WIFI_QR_SIZE, ESP_LOG_DEBUG);

    ssd1306_send_cmd_byte(I2C_NUM_1, SSD1306_DISPLAY_INVERSE);
    ssd1306_set_range(I2C_NUM_1, 0, 47, 0, 3);
    for (int y = 0; y < 32; y += 8) {
        for (int x = 0; x < 48; ++x) {
            uint8_t v = 0;
            for (uint8_t yb = 0; yb < 8; ++yb) {
                if (qrcodegen_getModule(qrcode, x - 8, y + yb - 1)) {
                    v |= 1 << yb;
                }

            }
            ssd1306_send_data_byte(I2C_NUM_1, v);
        }
    }

    {
        char linebuf[32];
        snprintf(linebuf, sizeof(linebuf), "SSID: %s", AP_SSID);
        lcd_puts(8, 0, linebuf);
        lcd_puts(8, 1, "Password:");
        lcd_puts(8, 2, AP_PASSWORD);
    }
}

void
display_conn_info(void) {
    ssd1306_send_cmd_byte(I2C_NUM_1, SSD1306_DISPLAY_NORMAL);
    ssd1306_set_range(I2C_NUM_1, 0, 127, 0, 3);
    ssd1306_memset(I2C_NUM_1, 0, 128 * 4);
    lcd_puts(0, 0, "Connected");
}



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
            ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
            break;
        }

        case SYSTEM_EVENT_STA_DISCONNECTED: {
            ESP_LOGE(TAG, "Disconnect reason : %d", info->disconnected.reason);
            if (info->disconnected.reason == WIFI_REASON_CONNECTION_FAIL) {
                esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N); // switch to 802.11 bgn mode
            }
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            break;
        }

        case SYSTEM_EVENT_AP_START: {
            ESP_LOGI(TAG, "AP started;");
            break;
        }

        case SYSTEM_EVENT_AP_STACONNECTED: {
            ESP_LOGI(TAG, "station:"MACSTR" join, AID=%d", MAC2STR(event->event_info.sta_connected.mac), event->event_info.sta_connected.aid);
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
            display_conn_info();
            break;
        }

        case SYSTEM_EVENT_AP_STADISCONNECTED: {
            ESP_LOGI(TAG, "station:"MACSTR" leave, AID=%d", MAC2STR(event->event_info.sta_disconnected.mac), event->event_info.sta_disconnected.aid);
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            display_ap_info();
            break;
        }

        default:
            break;
    }
    return ESP_OK;
}

#if 0
I (32863) wifi: new:<1,0>, old:<1,0>, ap:<1,1>, sta:<255,255>, prof:1
I (32863) wifi: station: 3c:17:f2:11:d9:77 join, AID=1, bgn, 20

I (32893) simple wifi: station:3c:17:f2:11:d9:77 join, AID=1
I (33833) tcpip_adapter: softAP assign IP to station,IP is: 192.168.4.2

I (49513) wifi: station: 3c:17:f2:11:d9:77 leave, AID = 1, bss_flags is 134259, bss:0x3ffb9c38
I (49513) wifi: new:<1,0>, old:<1,0>, ap:<1,1>, sta:<255,255>, prof:1

I (49513) simple wifi: station:3c:17:f2:11:d9:77 leave, AID=1
#endif

void
wifi_init_sta() {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = MY_SSID,
            .password = MY_PASSWORD
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

void
wifi_init_ap() {
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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_ap finished.");
}


void
app_main()
{
    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "This is ESP8266 chip with %d CPU cores, WiFi, silicon revision %d, %dMB %s flash\n",
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

    wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    //wifi_init_sta();
    wifi_init_ap();
    display_ap_info();


    /*for (int i = 10; i >= 0; i--) {
    ESP_LOGD(TAG, "Restarting in %d seconds...\n", i);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "Restarting now.\n");
    fflush(stdout);
    esp_restart(); */
}

// vim: set sw=4 ts=4 indk= et si:
