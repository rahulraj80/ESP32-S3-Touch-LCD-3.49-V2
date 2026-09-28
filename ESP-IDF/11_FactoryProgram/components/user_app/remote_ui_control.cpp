#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "lcd_bl_pwm_bsp.h"
#include "user_config.h"
#include "esp_io_expander_tca9554.h"
#include "remote_ui_control.h"

static const char *TAG = "REMOTE_CTRL";

// External LVGL locking from main.cpp
extern "C" {
bool example_lvgl_lock(int timeout_ms);
void example_lvgl_unlock(void);
}
extern esp_io_expander_handle_t io_expander;

// Global simulated touch variables
volatile bool g_sim_touch_active = false;
volatile uint16_t g_sim_touch_x = 0;
volatile uint16_t g_sim_touch_y = 0;

// Global panel handle
esp_lcd_panel_handle_t g_panel_handle = NULL;

static lv_ui *s_ui = NULL;
static lv_obj_t *s_options_cont = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_color_modal = NULL;
static lv_obj_t *s_cb_bl_kill = NULL;
static lv_obj_t *s_cb_invert = NULL;

static int s_current_page = 0;
static uint8_t s_current_bl = 255;
static bool s_is_dark = false;
static bool s_is_inverted = false;

// Forward declarations
static void color_btn_event_cb(lv_event_t *e);
static void cb_bl_event_cb(lv_event_t *e);
static void cb_invert_event_cb(lv_event_t *e);
static void preset_btn_event_cb(lv_event_t *e);
static void dismiss_modal_cb(lv_event_t *e);
static void update_status_ui(void);

// Simulated Tap
void remote_sim_tap(uint16_t x, uint16_t y)
{
    ESP_LOGI(TAG, "[AGY-EVENT][USB-CMD] SIMULATING TAP at (%d, %d)", x, y);
    g_sim_touch_x = x;
    g_sim_touch_y = y;
    g_sim_touch_active = true;
    vTaskDelay(pdMS_TO_TICKS(120));
    g_sim_touch_active = false;
    vTaskDelay(pdMS_TO_TICKS(50));
    update_status_ui();
}

// Simulated Swipe
void remote_sim_swipe(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t duration_ms)
{
    if (duration_ms < 50) duration_ms = 200;
    int steps = duration_ms / 15;
    if (steps < 3) steps = 3;

    ESP_LOGI(TAG, "[AGY-EVENT][USB-CMD] SIMULATING SWIPE from (%d, %d) to (%d, %d) in %d steps", x1, y1, x2, y2, steps);
    for (int i = 0; i <= steps; i++) {
        g_sim_touch_x = x1 + (x2 - x1) * i / steps;
        g_sim_touch_y = y1 + (y2 - y1) * i / steps;
        g_sim_touch_active = true;
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    g_sim_touch_active = false;
    vTaskDelay(pdMS_TO_TICKS(50));
    update_status_ui();
}

// Jump carousel page
void remote_set_page(int page_idx)
{
    if (!s_ui || !s_ui->screen_carousel_1) return;
    if (page_idx < 0) page_idx = 0;
    if (page_idx > 2) page_idx = 2;

    ESP_LOGI(TAG, "[AGY-EVENT][PAGE] JUMPING to carousel page %d", page_idx);
    if (example_lvgl_lock(500)) {
        lv_obj_scroll_to_x(s_ui->screen_carousel_1, page_idx * 172, LV_ANIM_ON);
        s_current_page = page_idx;
        example_lvgl_unlock();
    }
    update_status_ui();
}

// Set backlight brightness
void remote_set_backlight(uint8_t duty)
{
    s_current_bl = duty;
    setUpduty(0xff - duty);
    if (duty > 0 && s_is_dark) {
        if (io_expander) esp_io_expander_set_level(io_expander, EXAMPLE_EXIO_PIN_BL_EN, 1);
        gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, 0);
        s_is_dark = false;
    }
    ESP_LOGI(TAG, "[AGY-EVENT][BL] Backlight updated to %d/255 (%.1f%%)", duty, (duty * 100.0f) / 255.0f);

    if (s_ui && s_ui->screen_slider_1) {
        if (example_lvgl_lock(200)) {
            lv_slider_set_value(s_ui->screen_slider_1, duty, LV_ANIM_OFF);
            example_lvgl_unlock();
        }
    }
    update_status_ui();
}

// Display solid color test overlay
void remote_test_color(const char *color_name)
{
    if (!s_ui) return;
    ESP_LOGI(TAG, "[AGY-EVENT][DIAG] Solid color test requested: %s", color_name);

    if (strcmp(color_name, "none") == 0 || strcmp(color_name, "clear") == 0) {
        if (s_color_modal) {
            if (example_lvgl_lock(500)) {
                lv_obj_del(s_color_modal);
                s_color_modal = NULL;
                example_lvgl_unlock();
            }
        }
        update_status_ui();
        return;
    }

    lv_color_t color;
    if (strcmp(color_name, "red") == 0) {
        color = lv_color_hex(0xFF0000);
    } else if (strcmp(color_name, "green") == 0) {
        color = lv_color_hex(0x00FF00);
    } else if (strcmp(color_name, "blue") == 0) {
        color = lv_color_hex(0x0000FF);
    } else if (strcmp(color_name, "white") == 0) {
        color = lv_color_hex(0xFFFFFF);
    } else if (strcmp(color_name, "black") == 0 || strcmp(color_name, "dark") == 0) {
        color = lv_color_hex(0x000000);
    } else {
        color = lv_color_hex(0xFF0000);
    }

    if (example_lvgl_lock(500)) {
        if (!s_color_modal) {
            s_color_modal = lv_obj_create(s_ui->screen);
            lv_obj_set_pos(s_color_modal, 0, 0);
            lv_obj_set_size(s_color_modal, 172, 640);
            lv_obj_set_scrollbar_mode(s_color_modal, LV_SCROLLBAR_MODE_OFF);
            lv_obj_add_event_cb(s_color_modal, dismiss_modal_cb, LV_EVENT_CLICKED, NULL);

            lv_obj_t *lbl = lv_label_create(s_color_modal);
            lv_label_set_text_fmt(lbl, "TEST: %s\n(Tap to exit)", color_name);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), LV_PART_MAIN);
            lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        }
        lv_obj_set_style_bg_color(s_color_modal, color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_color_modal, 255, LV_PART_MAIN);
        lv_obj_move_foreground(s_color_modal);
        example_lvgl_unlock();
    }
    update_status_ui();
}

// Invert display colors
void remote_set_invert(bool invert)
{
    s_is_inverted = invert;
    if (g_panel_handle) {
        esp_lcd_panel_invert_color(g_panel_handle, invert);
        ESP_LOGI(TAG, "[AGY-EVENT][DIAG] Color Inversion: %s", invert ? "ENABLED (INVON)" : "DISABLED (INVOFF)");
    }
    if (s_cb_invert) {
        if (example_lvgl_lock(200)) {
            if (invert) lv_obj_add_state(s_cb_invert, LV_STATE_CHECKED);
            else lv_obj_clear_state(s_cb_invert, LV_STATE_CHECKED);
            example_lvgl_unlock();
        }
    }
    update_status_ui();
}

static void dismiss_modal_cb(lv_event_t *e)
{
    if (s_color_modal) {
        lv_obj_del(s_color_modal);
        s_color_modal = NULL;
        ESP_LOGI(TAG, "[AGY-EVENT][DIAG] Color Test Dismissed by user tap");
    }
}

static void update_status_ui(void)
{
    if (!s_status_label) return;
    if (example_lvgl_lock(200)) {
        lv_label_set_text_fmt(s_status_label,
            "Page: %d/2 | BL: %d\n"
            "Dark: %s | Inv: %s\n"
            "SimTouch: %d,%d (%s)",
            s_current_page, s_current_bl,
            s_is_dark ? "YES" : "NO",
            s_is_inverted ? "YES" : "NO",
            g_sim_touch_x, g_sim_touch_y,
            g_sim_touch_active ? "DOWN" : "UP");
        example_lvgl_unlock();
    }
}

static void color_btn_event_cb(lv_event_t *e)
{
    const char *col = (const char *)lv_event_get_user_data(e);
    remote_test_color(col);
}

static void cb_bl_event_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);
    s_is_dark = checked;
    if (checked) {
        ESP_LOGI(TAG, "[AGY-EVENT][BL] Kill Backlight -> Complete DARK");
        if (io_expander) esp_io_expander_set_level(io_expander, EXAMPLE_EXIO_PIN_BL_EN, 0);
        gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, 1);
    } else {
        ESP_LOGI(TAG, "[AGY-EVENT][BL] Restore Backlight");
        if (io_expander) esp_io_expander_set_level(io_expander, EXAMPLE_EXIO_PIN_BL_EN, 1);
        gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, 0);
        remote_set_backlight(s_current_bl);
    }
    update_status_ui();
}

static void cb_invert_event_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);
    remote_set_invert(checked);
}

static void preset_btn_event_cb(lv_event_t *e)
{
    int val = (int)(intptr_t)lv_event_get_user_data(e);
    remote_set_backlight((uint8_t)val);
}

// Print full status
void remote_print_status(void)
{
    uint32_t free_sram = esp_get_free_internal_heap_size();
    uint32_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    printf("{\"agy_status\": \"ok\", \"page\": %d, \"bl\": %d, \"dark\": %s, \"invert\": %s, \"free_sram\": %lu, \"free_psram\": %lu}\r\n",
           s_current_page, s_current_bl, s_is_dark ? "true" : "false", s_is_inverted ? "true" : "false",
           (unsigned long)free_sram, (unsigned long)free_psram);
}

// Build the Options Panel on carousel element 3
void init_diag_options_panel(lv_ui *ui)
{
    s_ui = ui;
    ESP_LOGI(TAG, "Initializing Diagnostics & Options Panel (Carousel Element 3)...");

    // Add element 3 to carousel
    lv_obj_t *el3 = lv_carousel_add_element(ui->screen_carousel_1, 2);
    lv_obj_set_style_bg_opa(el3, 255, LV_PART_MAIN);
        lv_obj_set_style_bg_color(el3, lv_color_hex(0x0a0a0a), LV_PART_MAIN);

        // Container with scrollbar for options
        s_options_cont = lv_obj_create(el3);
        lv_obj_set_pos(s_options_cont, 0, 0);
        lv_obj_set_size(s_options_cont, 172, 640);
        lv_obj_set_style_bg_opa(s_options_cont, 255, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_options_cont, lv_color_hex(0x0f111a), LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_options_cont, 6, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(s_options_cont, LV_SCROLLBAR_MODE_AUTO);

        // Title
        lv_obj_t *title = lv_label_create(s_options_cont);
        lv_label_set_text(title, "DIAGNOSTICS");
        lv_obj_set_style_text_color(title, lv_color_hex(0x00E5FF), LV_PART_MAIN);
        lv_obj_set_style_text_font(title, &lv_font_montserratMedium_16, LV_PART_MAIN);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

        lv_obj_t *subtitle = lv_label_create(s_options_cont);
        lv_label_set_text(subtitle, "USB Remote Active");
        lv_obj_set_style_text_color(subtitle, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 32);

        // Section 1: Color Tests
        lv_obj_t *sec1 = lv_label_create(s_options_cont);
        lv_label_set_text(sec1, "-- Solid Tests --");
        lv_obj_set_style_text_color(sec1, lv_color_hex(0xFFB300), LV_PART_MAIN);
        lv_obj_align(sec1, LV_ALIGN_TOP_MID, 0, 60);

        const char *colors[] = {"red", "green", "blue", "white", "black"};
        lv_color_t btn_colors[] = {lv_color_hex(0xD32F2F), lv_color_hex(0x388E3C), lv_color_hex(0x1976D2), lv_color_hex(0xEEEEEE), lv_color_hex(0x212121)};
        const char *labels[] = {"SOLID RED", "SOLID GREEN", "SOLID BLUE", "SOLID WHITE", "PITCH BLACK"};

        for (int i = 0; i < 5; i++) {
            lv_obj_t *btn = lv_btn_create(s_options_cont);
            lv_obj_set_size(btn, 150, 36);
            lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 85 + i * 42);
            lv_obj_set_style_bg_color(btn, btn_colors[i], LV_PART_MAIN);
            lv_obj_add_event_cb(btn, color_btn_event_cb, LV_EVENT_CLICKED, (void *)colors[i]);

            lv_obj_t *blbl = lv_label_create(btn);
            lv_label_set_text(blbl, labels[i]);
            lv_obj_set_style_text_color(blbl, (i == 3) ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF), LV_PART_MAIN);
            lv_obj_center(blbl);
        }

        // Section 2: Hardware Toggles
        lv_obj_t *sec2 = lv_label_create(s_options_cont);
        lv_label_set_text(sec2, "-- Hardware Options --");
        lv_obj_set_style_text_color(sec2, lv_color_hex(0xFFB300), LV_PART_MAIN);
        lv_obj_align(sec2, LV_ALIGN_TOP_MID, 0, 305);

        // Checkbox: Backlight Kill
        s_cb_bl_kill = lv_checkbox_create(s_options_cont);
        lv_checkbox_set_text(s_cb_bl_kill, "Kill BL (Dark)");
        lv_obj_set_style_text_color(s_cb_bl_kill, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_align(s_cb_bl_kill, LV_ALIGN_TOP_LEFT, 10, 330);
        lv_obj_add_event_cb(s_cb_bl_kill, cb_bl_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

        // Checkbox: Invert Color
        s_cb_invert = lv_checkbox_create(s_options_cont);
        lv_checkbox_set_text(s_cb_invert, "Invert Colors");
        lv_obj_set_style_text_color(s_cb_invert, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_align(s_cb_invert, LV_ALIGN_TOP_LEFT, 10, 365);
        lv_obj_add_event_cb(s_cb_invert, cb_invert_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

        // Section 3: Brightness Presets
        lv_obj_t *sec3 = lv_label_create(s_options_cont);
        lv_label_set_text(sec3, "-- Brightness --");
        lv_obj_set_style_text_color(sec3, lv_color_hex(0xFFB300), LV_PART_MAIN);
        lv_obj_align(sec3, LV_ALIGN_TOP_MID, 0, 405);

        int presets[] = {25, 64, 128, 192, 255};
        const char *plabels[] = {"10%", "25%", "50%", "75%", "100%"};
        for (int i = 0; i < 5; i++) {
            lv_obj_t *pbtn = lv_btn_create(s_options_cont);
            lv_obj_set_size(pbtn, 68, 30);
            int col = i % 2;
            int row = i / 2;
            lv_obj_set_pos(pbtn, 12 + col * 76, 430 + row * 36);
            lv_obj_set_style_bg_color(pbtn, lv_color_hex(0x283593), LV_PART_MAIN);
            lv_obj_add_event_cb(pbtn, preset_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)presets[i]);

            lv_obj_t *plbl = lv_label_create(pbtn);
            lv_label_set_text(plbl, plabels[i]);
            lv_obj_set_style_text_color(plbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
            lv_obj_center(plbl);
        }

        // Section 4: Telemetry Label
        s_status_label = lv_label_create(s_options_cont);
        lv_label_set_text(s_status_label, "Page: 2/2 | BL: 255\nUSB: Ready");
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x76FF03), LV_PART_MAIN);
        lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 550);

    ESP_LOGI(TAG, "Diagnostics & Options Panel created successfully!");
}

// USB Console Task ("ADB for ESP32")
static void usb_console_task(void *arg)
{
    ESP_LOGI(TAG, "=== USB Remote Control Task Active ('ADB for ESP32') ===");
    ESP_LOGI(TAG, "Type 'help' over COM11 for available commands");

    char line_buf[128];
    int line_idx = 0;

    for (;;) {
        int c = getchar();
        if (c == EOF || c < 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // Handle backspace
        if (c == '\b' || c == 127) {
            if (line_idx > 0) {
                line_idx--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
            continue;
        }

        // Echo char
        putchar(c);

        if (c == '\r' || c == '\n') {
            putchar('\n');
            line_buf[line_idx] = '\0';

            // Trim leading spaces
            char *cmd = line_buf;
            while (*cmd == ' ') cmd++;

            if (strlen(cmd) > 0) {
                ESP_LOGI(TAG, "[AGY-EVENT][USB-CMD] Received: '%s'", cmd);

                if (strcmp(cmd, "help") == 0) {
                    printf("\r\n=== ESP32-S3 Touch LCD 3.49 USB Console ('ADB') ===\r\n");
                    printf("  help                         - Show this help\r\n");
                    printf("  page <0|1|2>                 - Switch carousel page (0=Sensors, 1=Slider, 2=Options)\r\n");
                    printf("  next / prev                  - Switch to next/previous carousel page\r\n");
                    printf("  tap <x> <y>                  - Simulate touch tap at (x,y)\r\n");
                    printf("  swipe <x1> <y1> <x2> <y2>    - Simulate swipe from (x1,y1) to (x2,y2)\r\n");
                    printf("  bl <0-255>                   - Set backlight brightness\r\n");
                    printf("  color <red|green|blue|white|black|none> - Trigger solid test color\r\n");
                    printf("  dark                         - Turn backlight completely off\r\n");
                    printf("  light                        - Turn backlight on\r\n");
                    printf("  invert <0|1>                 - Invert display colors\r\n");
                    printf("  status                       - Print JSON status telemetry\r\n");
                    printf("====================================================\r\n");
                }
                else if (strncmp(cmd, "tap ", 4) == 0) {
                    uint16_t x = 0, y = 0;
                    if (sscanf(cmd + 4, "%hu %hu", &x, &y) == 2) {
                        remote_sim_tap(x, y);
                        printf("OK: Tapped at (%d, %d)\r\n", x, y);
                    } else {
                        printf("ERR: Usage: tap <x> <y>\r\n");
                    }
                }
                else if (strncmp(cmd, "swipe ", 6) == 0) {
                    uint16_t x1 = 0, y1 = 0, x2 = 0, y2 = 0, ms = 200;
                    int n = sscanf(cmd + 6, "%hu %hu %hu %hu %hu", &x1, &y1, &x2, &y2, &ms);
                    if (n >= 4) {
                        remote_sim_swipe(x1, y1, x2, y2, ms);
                        printf("OK: Swiped from (%d, %d) to (%d, %d)\r\n", x1, y1, x2, y2);
                    } else {
                        printf("ERR: Usage: swipe <x1> <y1> <x2> <y2> [ms]\r\n");
                    }
                }
                else if (strncmp(cmd, "page ", 5) == 0) {
                    int p = atoi(cmd + 5);
                    remote_set_page(p);
                    printf("OK: Page set to %d\r\n", p);
                }
                else if (strcmp(cmd, "next") == 0) {
                    remote_set_page((s_current_page + 1) % 3);
                    printf("OK: Next page\r\n");
                }
                else if (strcmp(cmd, "prev") == 0) {
                    remote_set_page((s_current_page + 2) % 3);
                    printf("OK: Prev page\r\n");
                }
                else if (strncmp(cmd, "bl ", 3) == 0) {
                    int val = atoi(cmd + 3);
                    if (val < 0) val = 0;
                    if (val > 255) val = 255;
                    remote_set_backlight((uint8_t)val);
                    printf("OK: Backlight set to %d\r\n", val);
                }
                else if (strncmp(cmd, "color ", 6) == 0) {
                    remote_test_color(cmd + 6);
                    printf("OK: Test color set to %s\r\n", cmd + 6);
                }
                else if (strcmp(cmd, "red") == 0) {
                    remote_test_color("red");
                    printf("OK: Test color RED\r\n");
                }
                else if (strcmp(cmd, "green") == 0) {
                    remote_test_color("green");
                    printf("OK: Test color GREEN\r\n");
                }
                else if (strcmp(cmd, "blue") == 0) {
                    remote_test_color("blue");
                    printf("OK: Test color BLUE\r\n");
                }
                else if (strcmp(cmd, "white") == 0) {
                    remote_test_color("white");
                    printf("OK: Test color WHITE\r\n");
                }
                else if (strcmp(cmd, "dark") == 0 || strcmp(cmd, "black") == 0) {
                    if (io_expander) esp_io_expander_set_level(io_expander, EXAMPLE_EXIO_PIN_BL_EN, 0);
                    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, 1);
                    s_is_dark = true;
                    ESP_LOGI(TAG, "[AGY-EVENT][BL] DARK MODE activated");
                    printf("OK: Dark mode (BL OFF)\r\n");
                    update_status_ui();
                }
                else if (strcmp(cmd, "light") == 0) {
                    if (io_expander) esp_io_expander_set_level(io_expander, EXAMPLE_EXIO_PIN_BL_EN, 1);
                    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, 0);
                    s_is_dark = false;
                    remote_set_backlight(s_current_bl);
                    ESP_LOGI(TAG, "[AGY-EVENT][BL] Backlight restored");
                    printf("OK: Backlight restored\r\n");
                    update_status_ui();
                }
                else if (strncmp(cmd, "invert ", 7) == 0) {
                    int inv = atoi(cmd + 7);
                    remote_set_invert(inv != 0);
                    printf("OK: Invert set to %d\r\n", inv);
                }
                else if (strcmp(cmd, "status") == 0) {
                    remote_print_status();
                }
                else {
                    printf("ERR: Unknown command '%s'. Type 'help'.\r\n", cmd);
                }
            }
            line_idx = 0;
        } else {
            if (line_idx < sizeof(line_buf) - 1) {
                line_buf[line_idx++] = (char)c;
            }
        }
    }
}

void start_usb_remote_control(lv_ui *ui)
{
    init_diag_options_panel(ui);
    xTaskCreatePinnedToCore(usb_console_task, "usb_console", 4096, NULL, 3, NULL, 1);
}
