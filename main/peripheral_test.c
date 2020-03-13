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
#include <esp_https_server.h>

#include <nvs_flash.h>

#include <driver/i2c.h>

#include <stdio.h>
#include <string.h>

#define SSD1306_I2C I2C_NUM_1

static const char *TAG = "ptest";

extern const uint8_t private_key_start[] asm("_binary_private_key_start");
extern const uint8_t private_key_end[] asm("_binary_private_key_end");
extern const uint8_t server_crt_start[] asm("_binary_server_crt_start");
extern const uint8_t server_crt_end[] asm("_binary_server_crt_end");

// FreeRTOS event group to signal when we are connected
static EventGroupHandle_t wifi_event_group;

// The event group allows multiple bits for each event, but we only care about one event - are we connected to the AP with an IP?
const int WIFI_CONNECTED_BIT = BIT0;

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
    ssd1306_set_range(SSD1306_I2C, col, col + 5, row, row);
    ssd1306_send_data(SSD1306_I2C, &font6x8[6 * (c - 0x20)], 6);
}


void
lcd_puts(int col, int row, const char *s) {
    if (!*s) {
        return;
    }
    col *= 6;
    ssd1306_set_range(SSD1306_I2C, col, 127, row, row);
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
    i2c_master_cmd_begin(SSD1306_I2C, cmd, 1000);
    i2c_cmd_link_delete(cmd);
}


bool
lcd_QR(uint8_t *tempBuffer, size_t input_length) {
    uint8_t qrcode[WIFI_QR_SIZE];

    if (!qrcodegen_encodeBinary(tempBuffer, input_length, qrcode, qrcodegen_Ecc_LOW, WIFI_QR_VERSION, WIFI_QR_VERSION, qrcodegen_Mask_AUTO, true)) {
        ESP_LOGE(TAG, "Failed to generate QR code;");
        return false;
    }
    //ESP_LOG_BUFFER_HEXDUMP(TAG, qrcode, WIFI_QR_SIZE, ESP_LOG_DEBUG);
    ssd1306_set_range(SSD1306_I2C, 0, 47, 0, 3);
    for (int y = 0; y < 32; y += 8) {
        for (int x = 0; x < 48; ++x) {
            uint8_t v = 0;
            for (uint8_t yb = 0; yb < 8; ++yb) {
                if (qrcodegen_getModule(qrcode, x - 8, y + yb - 1)) {
                    v |= 1 << yb;
                }

            }
            ssd1306_send_data_byte(SSD1306_I2C, v);
        }
    }
    return true;
}


void
display_wifi_conn(void) {
    ssd1306_clear(SSD1306_I2C);
    ssd1306_send_cmd_byte(SSD1306_I2C, SSD1306_DISPLAY_INVERSE);

    lcd_puts(8, 0, "SSID:");
    lcd_puts(8, 1, AP_SSID);
    lcd_puts(8, 2, "Password:");
    lcd_puts(8, 3, AP_PASSWORD);

    {
        // Encoding wifi parameters on QR-Code: https://github.com/zxing/zxing/wiki/Barcode-Contents#wi-fi-network-config-android-ios-11
        uint8_t tempBuffer[WIFI_QR_SIZE];
        size_t input_length = snprintf((char*)tempBuffer, WIFI_QR_SIZE, "WIFI:S:%s;T:WPA;P:%s;;", AP_SSID, AP_PASSWORD);
        lcd_QR(tempBuffer, input_length);
    }
}


void
display_portal_url(void) {
    char str_ip[16];

    ssd1306_clear(SSD1306_I2C);
    ssd1306_send_cmd_byte(SSD1306_I2C, SSD1306_DISPLAY_INVERSE);

    {
        tcpip_adapter_ip_info_t ap_info;
        tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ap_info);
        ip4addr_ntoa_r(&ap_info.ip, str_ip, sizeof(str_ip));
    }

    lcd_puts(8, 0, "https://");
    lcd_puts(8, 1, str_ip);

    {
        // Encoding URLs on QR-Code: https://github.com/zxing/zxing/wiki/Barcode-Contents#url
        uint8_t tempBuffer[WIFI_QR_SIZE];
        size_t input_length = snprintf((char*)tempBuffer, sizeof(tempBuffer), "https://%s", str_ip);
        lcd_QR(tempBuffer, input_length);
    }

}


static esp_err_t
root_get_handler(httpd_req_t *req) {
    ESP_LOGD(TAG, "root_get_handler;");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, "<h1>Hello Secure World!</h1>", -1); // -1 = use strlen()
    return ESP_OK;
}


static const
httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler
};


static httpd_handle_t
start_webserver(void) {
    httpd_handle_t server = NULL;

    ESP_LOGI(TAG, "Starting https server;");

    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
    conf.cacert_pem = server_crt_start;
    conf.cacert_len = server_crt_end - server_crt_start;
    conf.prvtkey_pem = private_key_start;
    conf.prvtkey_len = private_key_end - private_key_start;

    esp_err_t ret = httpd_ssl_start(&server, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error starting server; status=%d", ret);
        return NULL;
    }

    httpd_register_uri_handler(server, &root);
    return server;
}

httpd_handle_t* https_server = NULL;

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
            tcpip_adapter_ip_info_t ap_info;
            char str_ip[16], str_netmask[16], str_gw[16];

            tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ap_info);
            ip4addr_ntoa_r(&ap_info.ip, str_ip, sizeof(str_ip));
            ip4addr_ntoa_r(&ap_info.netmask, str_netmask, sizeof(str_netmask));
            ip4addr_ntoa_r(&ap_info.gw, str_gw, sizeof(str_gw));
            ESP_LOGI(TAG, "AP started; ip=%s, netmask=%s, gw=%s", str_ip, str_netmask, str_gw);
            display_wifi_conn();
            if (https_server == NULL) {
                https_server = start_webserver();
            }
            break;
        }

        case SYSTEM_EVENT_AP_STACONNECTED: {
            ESP_LOGI(TAG, "station:"MACSTR" join, AID=%d", MAC2STR(event->event_info.sta_connected.mac), event->event_info.sta_connected.aid);
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
            display_portal_url();
            break;
        }

        case SYSTEM_EVENT_AP_STADISCONNECTED: {
            ESP_LOGI(TAG, "station:"MACSTR" leave, AID=%d", MAC2STR(event->event_info.sta_disconnected.mac), event->event_info.sta_disconnected.aid);
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            display_wifi_conn();
            break;
        }

        default:
            break;
    }
    return ESP_OK;
}


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
    ssd1306_init(SSD1306_I2C, 23, 22);

    wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    {
        tcpip_adapter_ip_info_t ap_info = {
            .ip = { .addr = 0x0100000aUL },         // 10.0.0.1
            .netmask = { . addr = IP_CLASSA_NET },  // 255.0.0.0
            .gw = { .addr = IPADDR_NONE },
        };
        ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
        ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &ap_info));
        ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));
    }
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    //wifi_init_sta();
    wifi_init_ap();

    /*for (int i = 10; i >= 0; i--) {
    ESP_LOGD(TAG, "Restarting in %d seconds...\n", i);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "Restarting now.\n");
    fflush(stdout);
    esp_restart(); */
}


// vim: set sw=4 ts=4 indk= et si:
