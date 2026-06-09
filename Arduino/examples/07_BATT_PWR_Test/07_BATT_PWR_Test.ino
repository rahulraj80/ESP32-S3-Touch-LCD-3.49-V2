#include "user_config.h"
#include "driver/gpio.h"
#include "lvgl_port.h"
#include "esp_err.h"
#include "src/button_bsp/button_bsp.h"
#include "src/tca9554/esp_io_expander_tca9554.h"
#include "i2c_bsp.h"
#include "src/ui_src/generated/gui_guider.h"

esp_io_expander_handle_t io_expander = NULL;
static bool is_vbatpowerflag = false;

void tca9554_init(void)
{
  if (io_expander != NULL) {
    return;
  }
  i2c_master_bus_handle_t tca9554_i2c_bus_ = NULL;
  ESP_ERROR_CHECK(i2c_master_get_bus_handle(0,&tca9554_i2c_bus_));
  ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(tca9554_i2c_bus_, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander));
	ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, EXAMPLE_EXIO_PIN_SYS_EN, IO_EXPANDER_OUTPUT));
  ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, EXAMPLE_EXIO_PIN_SYS_EN, 1));
}

static void example_button_pwr_task(void* arg)
{
  lv_ui *ui = (lv_ui *)arg;
  for (;;)
  {
    EventBits_t even = xEventGroupWaitBits(pwr_groups,set_bit_all,pdTRUE,pdFALSE,pdMS_TO_TICKS(2 * 1000));
    if(get_bit_button(even,0))
    {

    }
    else if(get_bit_button(even,1)) //长按
    {
		  if(is_vbatpowerflag)
		  {
		  	is_vbatpowerflag = false;
		  	ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, EXAMPLE_EXIO_PIN_SYS_EN, 0));
		  	lv_label_set_text(ui->screen_label_1, "OFF");
		  }
    }
    else if(get_bit_button(even,2))
    {
		  if(!is_vbatpowerflag)
		  {
		  	is_vbatpowerflag = true;
		  }
    }
  }
}

void power_Test(void *arg)
{
	if(gpio_get_level(EXAMPLE_PIN_NUM_SYS_OUT))
	{
		is_vbatpowerflag = true;
	}
	vTaskDelete(NULL); 
}


void setup()
{
  i2c_master_Init();
  tca9554_init();
  Serial.begin(115200);
  lvgl_port_init();
  lv_label_set_text(src_ui.screen_label_1, "ON");
  button_Init();

  xTaskCreatePinnedToCore(example_button_pwr_task, "example_button_pwr_task", 4 * 1024, &src_ui, 2, NULL,1);      //按钮事件     
	xTaskCreatePinnedToCore(power_Test, "power_Test", 4 * 1024, NULL, 3, NULL,1);                           		//audio
}

void loop()
{

}
