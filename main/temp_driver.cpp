#include "ds18b20.h"
#include "esp_log.h"
#include <esp_matter.h>
#include "esp_matter_attribute_utils.h"
#include "esp_matter_core.h"
#include "esp_matter_endpoint.h"
#include "onewire_bus.h"

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

struct sensor_reader_ctx {
	ds18b20_device_handle_t ds18b20;
	uint16_t temp_endpoint_id;
} s_ctx;

#define TODO_FAKE_TEMP true

#if TODO_FAKE_TEMP
	int fake_temp = 2000;
#endif

void temp_sensor_reader(void *arg)
{
	struct sensor_reader_ctx *s_ctx = (struct sensor_reader_ctx *)arg;
	float temperature;
#if !TODO_FAKE_TEMP

	ds18b20_device_handle_t dev_handle = s_ctx->ds18b20;
	ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion(dev_handle));
	ESP_ERROR_CHECK(ds18b20_get_temperature(dev_handle, &temperature));
	ESP_LOGI(__func__, "Temperature data read: %f", temperature);

	esp_matter_attr_val_t val = esp_matter_int(temperature * 100);
	attribute::update(s_ctx->temp_endpoint_id, TemperatureMeasurement::Id,
			  TemperatureMeasurement::Attributes::MeasuredValue::Id, &val);
#else
	esp_matter_attr_val_t val = esp_matter_int(fake_temp);
	attribute::update(s_ctx->temp_endpoint_id, TemperatureMeasurement::Id,
			  TemperatureMeasurement::Attributes::MeasuredValue::Id, &val);
	fake_temp += 50;
	if (fake_temp > 4000) fake_temp = 2000;
#endif
}

#define MAX_NR_DEV 1
int matter_temp_init(node_t *node, int gpio_pin)
{
	// install new 1-wire bus
#if !TODO_FAKE_TEMP
	onewire_bus_handle_t bus;
	onewire_bus_config_t bus_config = {
	    .bus_gpio_num = gpio_pin,
	};
	onewire_bus_rmt_config_t rmt_config = {
	    .max_rx_bytes = 10, // 1byte ROM command + 8byte ROM number + 1byte device command
	};
	ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus));
	ESP_LOGI(__func__, "1-Wire bus installed on GPIO%d", gpio_pin);

	int ds18b20_device_num = 0;
	onewire_device_iter_handle_t iter = NULL;
	onewire_device_t next_onewire_device;
	esp_err_t search_result = ESP_OK;
	ds18b20_device_handle_t temp_handle;

	// create 1-wire device iterator, which is used for device search
	ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
	ESP_LOGI(__func__, "Device iterator created, start searching...");
	do {
		search_result = onewire_device_iter_get_next(iter, &next_onewire_device);
		if (search_result == ESP_OK) { // found a new device, let's check if we can upgrade it to a DS18B20
			ds18b20_config_t ds_cfg = {};
			if (ds18b20_new_device(&next_onewire_device, &ds_cfg, &temp_handle) == ESP_OK) {
				ESP_LOGI(__func__, "Found a DS18B20[%d], address: %016llX", ds18b20_device_num,
					 next_onewire_device.address);
				ds18b20_device_num++;
				if (ds18b20_device_num >= MAX_NR_DEV) {
					ESP_LOGI(__func__, "Max DS18B20 number reached, stop searching...");
					break;
				}
			} else {
				ESP_LOGI(__func__, "Found an unknown device, address: %016llX",
					 next_onewire_device.address);
			}
		}
	} while (search_result != ESP_ERR_NOT_FOUND);
	ESP_ERROR_CHECK(onewire_del_device_iter(iter));
	ESP_LOGI(__func__, "Searching done, %d DS18B20 device(s) found", ds18b20_device_num);

	// set resolution for all DS18B20s
	for (int i = 0; i < ds18b20_device_num; i++) {
		// set resolution
		ESP_ERROR_CHECK(ds18b20_set_resolution(temp_handle, DS18B20_RESOLUTION_9B));
	}

	// get temperature from sensors one by one
	float temperature;
	vTaskDelay(pdMS_TO_TICKS(200));
	for (int i = 0; i < ds18b20_device_num; i++) {
		ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion(temp_handle));
		ESP_ERROR_CHECK(ds18b20_get_temperature(temp_handle, &temperature));
		ESP_LOGI(__func__, "temperature read from DS18B20[%d]: %.2fC", i, temperature);
	}
#else
	ds18b20_device_handle_t temp_handle;
#endif

	// temperature sensor endpoint
	temperature_sensor::config_t matter_temp_config;
	endpoint_t *temp_endpoint =
	    temperature_sensor::create(node, &matter_temp_config, ENDPOINT_FLAG_NONE, &temp_handle);
	if (temp_endpoint == nullptr) {
		ESP_LOGE(__func__, "Failed to create a temperature endpoint");
		abort();
	}
	uint16_t temp_endpoint_id = endpoint::get_id(temp_endpoint);
	ESP_LOGI(__func__, "Temp created with endpoint_id %d", temp_endpoint_id);
	// attribute_t *temp_attribute = attribute::get(temp_endpoint_id, TemperatureMeasurement::Id,
	// TemperatureMeasurement::Attributes::MeasuredValue::Id);
	s_ctx = {temp_handle, temp_endpoint_id};
	esp_timer_create_args_t timer_args = {
	    .callback = temp_sensor_reader,
	    .arg = &s_ctx,
	};
	esp_timer_handle_t timer_handle;
	if (ESP_OK != esp_timer_create(&timer_args, &timer_handle)) {
	     ESP_LOGE(__func__, "Failed to create esp timer");
	     abort();
	}
	if (ESP_OK != esp_timer_start_periodic(timer_handle, /* 30s in us */ 30000000)) {
	     ESP_LOGE(__func__, "Failed to start timer");
	     abort();
	}

	return ESP_OK;
}
