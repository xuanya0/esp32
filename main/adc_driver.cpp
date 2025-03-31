/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "adc_driver.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------
	ADC General Macros
---------------------------------------------------------------*/
// ADC1 Channels
#if CONFIG_IDF_TARGET_ESP32
#define EXAMPLE_ADC1_CHAN0 ADC_CHANNEL_4
#define EXAMPLE_ADC1_CHAN1 ADC_CHANNEL_5
#else
#define EXAMPLE_ADC1_CHAN0 ADC_CHANNEL_2
#define EXAMPLE_ADC1_CHAN1 ADC_CHANNEL_3
#endif

#define EXAMPLE_ADC_ATTEN ADC_ATTEN_DB_12

static int adc_raw[2][10];
static int vol__func__e[2][10];
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten,
					 adc_cali_handle_t *out_handle);
static void example_adc_calibration_deinit(adc_cali_handle_t handle);

static adc_cali_handle_t adc1_cali_chan0_handle = NULL;
static adc_oneshot_unit_handle_t adc1_handle;
int adc_init(adc_unit_t unit, adc_atten_t atten, adc_channel_t channel)
{
	//-------------ADC1 Init---------------//
	adc_oneshot_unit_init_cfg_t init_config1 = {
	    .unit_id = unit,
	};
	ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

	//-------------ADC1 Config---------------//
	adc_oneshot_chan_cfg_t config = {atten, ADC_BITWIDTH_DEFAULT};
	ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, channel, &config));

	//-------------ADC1 Calibration Init---------------//
	bool do_calibration1_chan0 = example_adc_calibration_init(ADC_UNIT_1, channel, atten, &adc1_cali_chan0_handle);

	ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN0, &adc_raw[0][0]));
	ESP_LOGI(__func__, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, adc_raw[0][0]);
	if (do_calibration1_chan0) {
		ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw[0][0], &vol__func__e[0][0]));
		ESP_LOGI(__func__, "ADC%d Channel[%d] Cali Vol__func__e: %d mV", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0,
			 vol__func__e[0][0]);
	}
	vTaskDelay(pdMS_TO_TICKS(1000));
	return ESP_OK;
}

void adc_deinit(void)
{
	// Tear Down
	ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
	example_adc_calibration_deinit(adc1_cali_chan0_handle);
}

/*---------------------------------------------------------------
	ADC Calibration
---------------------------------------------------------------*/
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten,
					 adc_cali_handle_t *out_handle)
{
	adc_cali_handle_t handle = NULL;
	esp_err_t ret = ESP_FAIL;
	bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
	if (!calibrated) {
		ESP_LOGI(__func__, "calibration scheme version is %s", "Curve Fitting");
		adc_cali_curve_fitting_config_t cali_config = {
		    .unit_id = unit,
		    .chan = channel,
		    .atten = atten,
		    .bitwidth = ADC_BITWIDTH_DEFAULT,
		};
		ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
		if (ret == ESP_OK) {
			calibrated = true;
		}
	}
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
	if (!calibrated) {
		ESP_LOGI(__func__, "calibration scheme version is %s", "Line Fitting");
		adc_cali_line_fitting_config_t cali_config = {
		    .unit_id = unit,
		    .atten = atten,
		    .bitwidth = ADC_BITWIDTH_DEFAULT,
		};
		ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
		if (ret == ESP_OK) {
			calibrated = true;
		}
	}
#endif

	*out_handle = handle;
	if (ret == ESP_OK) {
		ESP_LOGI(__func__, "Calibration Success");
	} else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
		ESP_LOGW(__func__, "eFuse not burnt, skip software calibration");
	} else {
		ESP_LOGE(__func__, "Invalid arg or no memory");
	}

	return calibrated;
}

static void example_adc_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
	ESP_LOGI(__func__, "deregister %s calibration scheme", "Curve Fitting");
	ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
	ESP_LOGI(__func__, "deregister %s calibration scheme", "Line Fitting");
	ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}
