idf_component_register(SRCS "main.cpp"
                    REQUIRES
                    esp_timer
                    esp_lcd
                    espressif__esp_lcd_axs15231b
                    espressif__esp_io_expander_tca9554
                    i2c_bsp
                    lvgl
                    user_app
                    INCLUDE_DIRS ".")
