#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_io_expander_tca9554.h"
#include "esp_lcd_axs15231b.h"

static const char *TAG = "pure_red";

#define LCD_HOST SPI3_HOST

// ESP32-S3 I2C pins for TCA9554
#define ESP_SCL_NUM (GPIO_NUM_48)
#define ESP_SDA_NUM (GPIO_NUM_47)

// Display QSPI pins (Official Waveshare V2 mapping)
#define LCD_PIN_CS     (GPIO_NUM_9) 
#define LCD_PIN_PCLK   (GPIO_NUM_10)
#define LCD_PIN_DATA0  (GPIO_NUM_11) 
#define LCD_PIN_DATA1  (GPIO_NUM_12)
#define LCD_PIN_DATA2  (GPIO_NUM_13)
#define LCD_PIN_DATA3  (GPIO_NUM_14)
#define LCD_PIN_BL     (GPIO_NUM_42)

// TCA9554 Pins
#define EXIO_PIN_BL_EN     (1ULL << 1)
#define EXIO_PIN_LCD_RST   (1ULL << 5)
#define EXIO_PIN_SYS_EN    (1ULL << 6)

#define LCD_H_RES 172
#define LCD_V_RES 640
#define FULL_FRAME_BYTES (LCD_H_RES * LCD_V_RES * 2) // 220,160 bytes

static esp_io_expander_handle_t io_expander = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static SemaphoreHandle_t trans_sem = NULL;
static uint8_t *full_frame_buf = NULL;

// Factory minimal init: ONLY Sleep Out and Display On (preserves factory OTP calibration!)
static const axs15231b_lcd_init_cmd_t factory_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 150}, // Sleep Out
    {0x13, (uint8_t[]){0x00}, 0, 50},  // Normal Display Mode On
    {0x29, (uint8_t[]){0x00}, 0, 100}, // Display On
};

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t high_task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(trans_sem, &high_task_awoken);
    return false;
}

static void init_io_expander(void)
{
    ESP_LOGI(TAG, "Initializing TCA9554...");
    i2c_master_bus_config_t i2c_bus_config = {};
    i2c_bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_config.i2c_port = I2C_NUM_0;
    i2c_bus_config.scl_io_num = ESP_SCL_NUM;
    i2c_bus_config.sda_io_num = ESP_SDA_NUM;
    i2c_bus_config.glitch_ignore_cnt = 7;
    i2c_bus_config.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus));
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(i2c_bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander));

    uint32_t output_pins = EXIO_PIN_BL_EN | EXIO_PIN_LCD_RST | EXIO_PIN_SYS_EN;
    ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, output_pins, IO_EXPANDER_OUTPUT));

    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, EXIO_PIN_SYS_EN, 1));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, EXIO_PIN_LCD_RST, 1));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, EXIO_PIN_BL_EN, 1));
    ESP_LOGI(TAG, "TCA9554 ready");
}

static void set_backlight(bool on)
{
    if (on) {
        gpio_set_level(LCD_PIN_BL, 0); // 0V = ON
        if (io_expander) esp_io_expander_set_level(io_expander, EXIO_PIN_BL_EN, 1);
        ESP_LOGI(TAG, "Backlight: ON");
    } else {
        gpio_set_level(LCD_PIN_BL, 1); // 3.3V = OFF
        if (io_expander) esp_io_expander_set_level(io_expander, EXIO_PIN_BL_EN, 0);
        ESP_LOGI(TAG, "Backlight: OFF (COMPLETE DARK)");
    }
}

static void send_full_red_frame(void)
{
    xSemaphoreTake(trans_sem, portMAX_DELAY);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, full_frame_buf));
    xSemaphoreTake(trans_sem, portMAX_DELAY);
    xSemaphoreGive(trans_sem);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== Waveshare AXS15231B Pure OTP RED & Dark Driver ===");

    // Backlight GPIO config
    gpio_config_t bl_gpio_conf = {};
    bl_gpio_conf.pin_bit_mask = (1ULL << LCD_PIN_BL);
    bl_gpio_conf.mode = GPIO_MODE_OUTPUT;
    bl_gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    bl_gpio_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    bl_gpio_conf.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&bl_gpio_conf));
    set_backlight(true);

    init_io_expander();

    trans_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(trans_sem);

    // Allocate full 220KB frame buffer in DMA-capable memory (PSRAM or SRAM)
    full_frame_buf = (uint8_t *)heap_caps_malloc(FULL_FRAME_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
    if (!full_frame_buf) {
        full_frame_buf = (uint8_t *)heap_caps_malloc(FULL_FRAME_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    assert(full_frame_buf);
    ESP_LOGI(TAG, "Allocated %d bytes full frame buffer at %p", FULL_FRAME_BYTES, full_frame_buf);

    // Fill the buffer with pure saturated RED in RGB565: 0xF800
    // Byte 0 (high byte): 0xF8, Byte 1 (low byte): 0x00
    for (int i = 0; i < FULL_FRAME_BYTES; i += 2) {
        full_frame_buf[i] = 0xF8;
        full_frame_buf[i + 1] = 0x00;
    }

    ESP_LOGI(TAG, "Initializing SPI3 QSPI bus (D0=11, D1=12, D2=13, D3=14)...");
    spi_bus_config_t buscfg = {};
    buscfg.data0_io_num = LCD_PIN_DATA0; // 11
    buscfg.data1_io_num = LCD_PIN_DATA1; // 12
    buscfg.data2_io_num = LCD_PIN_DATA2; // 13
    buscfg.data3_io_num = LCD_PIN_DATA3; // 14
    buscfg.sclk_io_num = LCD_PIN_PCLK;   // 10
    buscfg.max_transfer_sz = FULL_FRAME_BYTES;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = LCD_PIN_CS;
    io_config.dc_gpio_num = -1;
    io_config.spi_mode = 3;
    io_config.pclk_hz = 20 * 1000 * 1000; // 20 MHz clean clock
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = on_trans_done;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &panel_io));

    axs15231b_vendor_config_t vendor_config = {};
    vendor_config.flags.use_qspi_interface = 1;
    vendor_config.init_cmds = factory_init_cmds;
    vendor_config.init_cmds_size = sizeof(factory_init_cmds) / sizeof(factory_init_cmds[0]);

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config;
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &panel_handle));

    // Reload OTP via software reset command 0x01
    ESP_LOGI(TAG, "Resetting panel (reloads factory OTP)...");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    vTaskDelay(pdMS_TO_TICKS(150));

    ESP_LOGI(TAG, "Initializing panel...");
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    ESP_LOGI(TAG, "AXS15231B panel initialized with factory OTP");

    ESP_LOGI(TAG, ">>> Sending FULL SCREEN SATURATED RED (220KB continuous) <<<");
    send_full_red_frame();
    ESP_LOGI(TAG, "RED frame transmission complete!");

    while (1) {
        ESP_LOGI(TAG, "[STATE] SOLID RED - Backlight ON");
        send_full_red_frame();
        set_backlight(true);
        vTaskDelay(pdMS_TO_TICKS(15000));

        ESP_LOGI(TAG, "[STATE] COMPLETE DARK - Backlight OFF");
        set_backlight(false);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
