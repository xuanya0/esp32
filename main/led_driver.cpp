/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include <esp_matter.h>
#include <bsp/esp_bsp_devkit.h>

#include <app_priv.h>

using namespace chip::app::Clusters;
using namespace esp_matter;

led_indicator_handle_t bsp_leds[BSP_LED_NUM];

extern uint16_t light_endpoint_id;

/* Do any conversions/remapping for the actual value here */
static esp_err_t led_set_power(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
	return led_indicator_set_on_off(handle, val->val.b);
}

static esp_err_t led_set_hue(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
	int value = REMAP_TO_RANGE(val->val.u8, MATTER_HUE, STANDARD_HUE);
	led_indicator_ihsv_t hsv;
	hsv.value = led_indicator_get_hsv(handle);
	hsv.h = value;
	return led_indicator_set_hsv(handle, hsv.value);
}

static esp_err_t led_set_saturation(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
	int value = REMAP_TO_RANGE(val->val.u8, MATTER_HUE, STANDARD_HUE);
	led_indicator_ihsv_t hsv;
	hsv.value = led_indicator_get_hsv(handle);
	hsv.s = value;
	return led_indicator_set_hsv(handle, hsv.value);
}

static esp_err_t led_set_brightness(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
	int value = REMAP_TO_RANGE(val->val.u8, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS);
	led_indicator_ihsv_t hsv;
	hsv.value = led_indicator_get_hsv(handle);
	hsv.v = value;
	return led_indicator_set_hsv(handle, hsv.value);
}

static esp_err_t led_set_temperature(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
	uint32_t value = REMAP_TO_RANGE_INVERSE(val->val.u16, STANDARD_TEMPERATURE_FACTOR);
	return led_indicator_set_color_temperature(handle, value);
}


esp_err_t led_driver_attribute_update(void *matter_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    led_indicator_handle_t handle = (led_indicator_handle_t)matter_handle;
    if (endpoint_id == light_endpoint_id) {
        if (cluster_id == OnOff::Id) {
            if (attribute_id == OnOff::Attributes::OnOff::Id) {
                err = led_set_power(handle, val);
            }
        } else if (cluster_id == LevelControl::Id) {
            if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
                err = led_set_brightness(handle, val);
            }
        } else if (cluster_id == ColorControl::Id) {
            if (attribute_id == ColorControl::Attributes::CurrentHue::Id) {
                err = led_set_hue(handle, val);
            } else if (attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
                err = led_set_saturation(handle, val);
            } else if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
                err = led_set_temperature(handle, val);
            }
        }
    }
    return err;
}

esp_err_t led_driver_set_defaults(uint16_t endpoint_id)
{
    esp_err_t err = ESP_OK;
    void *priv_data = endpoint::get_priv_data(endpoint_id);
    led_indicator_handle_t handle = (led_indicator_handle_t)priv_data;
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);

    /* Setting brightness */
    attribute_t *attribute = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    attribute::get_val(attribute, &val);
    err |= led_set_brightness(handle, &val);

    /* Setting color */
    attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id);
    attribute::get_val(attribute, &val);
    if (val.val.u8 == (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation) {
        /* Setting hue */
        attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
        attribute::get_val(attribute, &val);
        err |= led_set_hue(handle, &val);
        /* Setting saturation */
        attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
        attribute::get_val(attribute, &val);
        err |= led_set_saturation(handle, &val);
    } else if (val.val.u8 == (uint8_t)ColorControl::ColorMode::kColorTemperature) {
        /* Setting temperature */
        attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
        attribute::get_val(attribute, &val);
        err |= led_set_temperature(handle, &val);
    } else {
        ESP_LOGE(__func__, "Color mode not supported");
    }

    /* Setting power */
    attribute = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    attribute::get_val(attribute, &val);
    err |= led_set_power(handle, &val);

    return err;
}

void led_driver_init()
{
	ESP_ERROR_CHECK(bsp_led_indicator_create(bsp_leds,  NULL, BSP_LED_NUM));
	ESP_ERROR_CHECK(led_indicator_set_rgb(bsp_leds[0], SET_IRGB(0, 0x64, 0x64, 0x64)));
	ESP_ERROR_CHECK(led_indicator_start(bsp_leds[0], BSP_LED_OFF));
}

int matter_board_led_init(node_t *node)
{
	using namespace esp_matter::endpoint;

	led_driver_init();

	extended_color_light::config_t light_config;
	light_config.on_off.on_off = DEFAULT_POWER;
	light_config.on_off.on_off = false;
	light_config.on_off.lighting.start_up_on_off = nullptr;
	light_config.level_control.current_level = DEFAULT_BRIGHTNESS / 4;
	light_config.level_control.on_level = DEFAULT_BRIGHTNESS / 4;
	light_config.level_control.lighting.start_up_current_level = DEFAULT_BRIGHTNESS / 4;
	light_config.color_control.color_mode = (uint8_t)ColorControl::ColorMode::kColorTemperature;
	light_config.color_control.enhanced_color_mode = (uint8_t)ColorControl::ColorMode::kColorTemperature;
	light_config.color_control.color_temperature.startup_color_temperature_mireds = nullptr;

	// endpoint handles can be used to add/modify clusters.
	endpoint_t *light_endpoint =
	    extended_color_light::create(node, &light_config, ENDPOINT_FLAG_NONE, bsp_leds[0]);
	if (light_endpoint == nullptr) {
	     ESP_LOGE(__func__, "Failed to create extended color light endpoint");
	     abort();
	}

	light_endpoint_id = endpoint::get_id(light_endpoint);
	ESP_LOGI(__func__, "Light created with endpoint_id %d", light_endpoint_id);

	/* Mark deferred persistence for some attributes that might be changed rapidly */
	attribute_t *current_level_attribute =
	    attribute::get(light_endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
	attribute::set_deferred_persistence(current_level_attribute);

	attribute_t *current_x_attribute =
	    attribute::get(light_endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentX::Id);
	attribute::set_deferred_persistence(current_x_attribute);
	attribute_t *current_y_attribute =
	    attribute::get(light_endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentY::Id);
	attribute::set_deferred_persistence(current_y_attribute);
	attribute_t *color_temp_attribute =
	    attribute::get(light_endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
	attribute::set_deferred_persistence(color_temp_attribute);

	return ESP_OK;
}
