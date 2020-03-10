#include "ssd1306.h"
#include <stdio.h>

#define TEST_PATTERNS 1

esp_err_t
ssd1306_send_cmd(i2c_port_t port, uint8_t code) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, 0x78, true);
    i2c_master_write_byte(cmd, 0x00, true);
    i2c_master_write_byte(cmd, code, true);
    i2c_master_stop(cmd);
    esp_err_t status = i2c_master_cmd_begin(port, cmd, 1000);
    i2c_cmd_link_delete(cmd);
    if (status != ESP_OK) {
        printf("ssd1306_send_cmd failed; code=0x%02x, status=0x%02x\n", code, status);
    }
    return status;
}

esp_err_t
ssd1306_send_data(i2c_port_t port, uint8_t value) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, 0x78, true);
    i2c_master_write_byte(cmd, 0x40, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    esp_err_t status = i2c_master_cmd_begin(port, cmd, 1000);
    i2c_cmd_link_delete(cmd);
    if (status != ESP_OK) {
        printf("ssd1306_send_data failed; value=0x%02x, status=0x%02x\n", value, status);
    }
    return status;
}

esp_err_t
ssd1306_init(i2c_port_t port, int sda_io, int scl_io) {
    esp_err_t status;
    i2c_config_t conf;

    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = sda_io;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = scl_io;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 100000;

    status = i2c_param_config(port, &conf);
    if (status != ESP_OK) {
        printf("i2c_param_config failed; status=0x%02x\n", status);
        return status;
    }
    status = i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0);
    if (status != ESP_OK) {
        printf("i2c_driver_install failed; status=0x%02x\n", status);
        return status;
    }

    // (Re)initialize the display
    // NOTE: Don't bother resetting values we never change. Noone else changes them either.
    ssd1306_send_cmd(port, SSD1306_DISPLAY_OFF);
    ssd1306_send_cmd(port, SSD1306_LAST_ROW);
    ssd1306_send_cmd(port, 0x1f);
    ssd1306_send_cmd(port, SSD1306_CHARGEPUMP);
    ssd1306_send_cmd(port, 0x14);
    ssd1306_send_cmd(port, SSD1306_ADDRESSING_MODE);
    ssd1306_send_cmd(port, 0);
    ssd1306_send_cmd(port, SSD1306_COM_PINS);
    ssd1306_send_cmd(port, 0x02);
    ssd1306_send_cmd(port, SSD1306_DISPLAY_ON);

#ifdef TEST_PATTERNS

    // Binary pattern to page 0 and 2 (== rows 0..15)
    ssd1306_send_cmd(port, SSD1306_COLUMN_RANGE);
    ssd1306_send_cmd(port, 0x00);
    ssd1306_send_cmd(port, 0x7f);
    ssd1306_send_cmd(port, SSD1306_PAGE_RANGE);
    ssd1306_send_cmd(port, 0x00);
    ssd1306_send_cmd(port, 0x03);
    for (uint16_t i = 0; i < 0x100; ++i) {
        ssd1306_send_data(port, i);
    }

    // Binary pattern to the bottom-right 16x16 pixels
    // columns 0x70..0x7f, rows 0x10..0x1f == pages 2..3
    ssd1306_send_cmd(port, SSD1306_COLUMN_RANGE);
    ssd1306_send_cmd(port, 0x70);
    ssd1306_send_cmd(port, 0x7f);
    ssd1306_send_cmd(port, SSD1306_PAGE_RANGE);
    ssd1306_send_cmd(port, 0x02);
    ssd1306_send_cmd(port, 0x03);
    for (uint8_t i = 0; i < 0x20; ++i) {
        ssd1306_send_data(port, i);
    }

#else // TEST_PATTERNS

    for (uint16_t i = 0; i < 128 * 32 / 8; ++i) {
        ssd1306_send_data(port, 0);
    }

#endif // TEST_PATTERNS

    return ESP_OK;
}

// vim: set sw=4 ts=4 indk= et si:
