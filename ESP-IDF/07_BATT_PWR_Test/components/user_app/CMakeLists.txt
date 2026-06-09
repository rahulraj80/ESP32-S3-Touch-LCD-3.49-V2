idf_component_register(
  SRCS "user_app.cpp"
  REQUIRES
  lvgl 
  ui_bsp 
  button_bsp 
  driver
  espressif__esp_io_expander_tca9554
  INCLUDE_DIRS "./" "../../main")
