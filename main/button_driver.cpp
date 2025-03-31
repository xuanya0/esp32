
/*
#include <esp_log.h>
#include <esp_matter.h>
#include "iot_button.h"

static const char *TAG = "app_reset";
static bool perform_factory_reset = false;

static void button_factory_reset_pressed_cb(void *arg, void *data)
{
    if (!perform_factory_reset) {
        ESP_LOGI(TAG, "Factory reset triggered. Release the button to start factory reset.");
        perform_factory_reset = true;
    }
}

static void button_factory_reset_released_cb(void *arg, void *data)
{
    if (perform_factory_reset) {
        ESP_LOGI(TAG, "Starting factory reset");
        esp_matter::factory_reset();
        perform_factory_reset = false;
    }
}

esp_err_t app_reset_button_register(void *handle)
{
    if (!handle) {
        ESP_LOGE(TAG, "Handle cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    button_handle_t button_handle = (button_handle_t)handle;
    esp_err_t err = ESP_OK;
    err |= iot_button_register_cb(button_handle, BUTTON_LONG_PRESS_HOLD, button_factory_reset_pressed_cb, NULL);
    err |= iot_button_register_cb(button_handle, BUTTON_PRESS_UP, button_factory_reset_released_cb, NULL);
    return err;
}


static void app_driver_button_toggle_cb(void *arg, void *data)
{
	ESP_LOGI(TAG, "Toggle button pressed");
	uint16_t endpoint_id = light_endpoint_id;
	uint32_t cluster_id = OnOff::Id;
	uint32_t attribute_id = OnOff::Attributes::OnOff::Id;

	attribute_t *attribute = attribute::get(endpoint_id, cluster_id, attribute_id);

	esp_matter_attr_val_t val = esp_matter_invalid(NULL);
	attribute::get_val(attribute, &val);
	val.val.b = !val.val.b;
	attribute::update(endpoint_id, cluster_id, attribute_id, &val);
}

app_driver_handle_t app_driver_button_init()
{
	button_handle_t btns[BSP_BUTTON_NUM];
	ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
	ESP_ERROR_CHECK(iot_button_register_cb(btns[0], BUTTON_PRESS_DOWN, app_driver_button_toggle_cb, NULL));

	return (app_driver_handle_t)btns[0];
}
*/
