#ifndef REMOTE_UI_CONTROL_H
#define REMOTE_UI_CONTROL_H

#include "lvgl.h"
#include "gui_guider.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

// Global simulated touch variables for LVGL indev driver
extern volatile bool g_sim_touch_active;
extern volatile uint16_t g_sim_touch_x;
extern volatile uint16_t g_sim_touch_y;

// Global panel handle for display operations (e.g. invert color)
extern esp_lcd_panel_handle_t g_panel_handle;

// Initialize Options & Diagnostics panel on carousel element 3
void init_diag_options_panel(lv_ui *ui);

// Start USB Serial Remote Control task (ADB-like interface)
void start_usb_remote_control(lv_ui *ui);

// High-level control actions
void remote_set_page(int page_idx);
void remote_set_backlight(uint8_t duty);
void remote_test_color(const char *color_name);
void remote_sim_tap(uint16_t x, uint16_t y);
void remote_sim_swipe(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t duration_ms);
void remote_set_invert(bool invert);
void remote_print_status(void);

#ifdef __cplusplus
}
#endif

#endif // REMOTE_UI_CONTROL_H
