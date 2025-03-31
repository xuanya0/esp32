/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_pm.h>

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <app_priv.h>
#include <platform/ESP32/OpenthreadLauncher.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
#include <esp_matter_providers.h>
#include <lib/support/Span.h>
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
#include <platform/ESP32/ESP32SecureCertDACProvider.h>
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
#include <platform/ESP32/ESP32FactoryDataProvider.h>
#endif
using namespace chip::DeviceLayer;
#endif

uint16_t light_endpoint_id = 0;
uint16_t temp_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
extern const uint8_t cd_start[] asm("_binary_certification_declaration_der_start");
extern const uint8_t cd_end[] asm("_binary_certification_declaration_der_end");

const chip::ByteSpan cdSpan(cd_start, static_cast<size_t>(cd_end - cd_start));
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

#if CONFIG_ENABLE_ENCRYPTED_OTA
extern const char decryption_key_start[] asm("_binary_esp_image_encryption_key_pem_start");
extern const char decryption_key_end[] asm("_binary_esp_image_encryption_key_pem_end");

static const char *s_decryption_key = decryption_key_start;
static const uint16_t s_decryption_key_len = decryption_key_end - decryption_key_start;
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
	switch (event->Type) {
	case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
		ESP_LOGI(__func__, "Interface IP Address changed");
		break;

	case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
		ESP_LOGI(__func__, "Commissioning complete");
		break;

	case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
		ESP_LOGI(__func__, "Commissioning failed, fail safe timer expired");
		break;

	case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
		ESP_LOGI(__func__, "Commissioning session started");
		break;

	case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
		ESP_LOGI(__func__, "Commissioning session stopped");
		break;

	case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
		ESP_LOGI(__func__, "Commissioning window opened");
		break;

	case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
		ESP_LOGI(__func__, "Commissioning window closed");
		break;

	case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
		ESP_LOGI(__func__, "Fabric removed successfully");
		if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
			chip::CommissioningWindowManager &commissionMgr =
			    chip::Server::GetInstance().GetCommissioningWindowManager();
			constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
			if (!commissionMgr.IsCommissioningWindowOpen()) {
				/* After removing last fabric, this example does not remove the Wi-Fi credentials
				 * and still has IP connectivity so, only advertising on DNS-SD.
				 */
				CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(
				    kTimeoutSeconds, chip::CommissioningWindowAdvertisement::kDnssdOnly);
				if (err != CHIP_NO_ERROR) {
					ESP_LOGE(__func__, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT,
						 err.Format());
				}
			}
		}
		break;
	}

	case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
		ESP_LOGI(__func__, "Fabric will be removed");
		break;

	case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
		ESP_LOGI(__func__, "Fabric is updated");
		break;

	case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
		ESP_LOGI(__func__, "Fabric is committed");
		break;

	case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
		ESP_LOGI(__func__, "BLE deinitialized and memory reclaimed");
		break;

	default:
		break;
	}
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
				       uint8_t effect_variant, void *priv_data)
{
	ESP_LOGI(__func__, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
	return ESP_OK;
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
					 uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
	esp_err_t err = ESP_OK;

	if (type == PRE_UPDATE) {
		if (endpoint_id == light_endpoint_id)
			return led_driver_attribute_update(priv_data, endpoint_id, cluster_id, attribute_id, val);
	}

	return err;
}

#define ONEWIRE_BUS_GPIO 0


void power_management_debug(void *arg) {
	ESP_LOGI(__func__, "DUMPING LOCK ===========  %d", esp_pm_dump_locks(stdout));
	ESP_LOGI(__func__, "DUMPING TIMER ===========  %d", esp_timer_dump(stdout));
}

extern "C" void app_main()
{
	esp_err_t err = ESP_OK;

	/* Initialize the ESP NVS layer */
	nvs_flash_init();
	ESP_LOGI(__func__, "FLASH NVS initialized");

	esp_pm_config_t pm_config = {
		.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
		.min_freq_mhz = CONFIG_XTAL_FREQ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
		.light_sleep_enable = true,
#endif
	};
	err = esp_pm_configure(&pm_config);

	/* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
	node::config_t node_config;

	// node handle can be used to add/modify other endpoints.
	node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
	if (node == nullptr) {
		ESP_LOGE(__func__, "Failed to create Matter node");
		abort();
	}
	ESP_LOGI(__func__, "matter node created");

	/* Adding matter devices here! */
	matter_board_led_init(node);
	ESP_LOGI(__func__, "board led initialized");
	matter_temp_init(node, ONEWIRE_BUS_GPIO);
	ESP_LOGI(__func__, "one_wire temp initialized");

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD && CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION
	// Enable secondary network interface
	secondary_network_interface::config_t secondary_network_interface_config;
	endpoint = endpoint::secondary_network_interface::create(node, &secondary_network_interface_config,
								 ENDPOINT_FLAG_NONE, nullptr);
	if (endpoint == nullptr) {
		ESP_LOGE(__func__, "Failed to create secondary network interface endpoint");
		abort();
	}
#endif

	/* Set OpenThread platform config */
	esp_openthread_platform_config_t config = {
	    .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
	    .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
	    .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
	};
	set_openthread_platform_config(&config);

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
	auto *dac_provider = get_dac_provider();
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
	static_cast<ESP32SecureCertDACProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
	static_cast<ESP32FactoryDataProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#endif
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

	/* Matter start */
	err = esp_matter::start(app_event_cb);
	if (err != ESP_OK) {
		ESP_LOGE(__func__, "Failed to start Matter, err:%d", err);
		abort();
	}
	ESP_LOGI(__func__, "========================Matter has started=========================");

	/* Starting driver with default values */
	led_driver_set_defaults(light_endpoint_id);

#if CONFIG_ENABLE_ENCRYPTED_OTA
	err = esp_matter_ota_requestor_encrypted_init(s_decryption_key, s_decryption_key_len);
	if (err != ESP_OK) {
		ESP_LOGE(__func__, "Failed to initialized the encrypted OTA, err: %d", err);
		abort();
	}
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

#if CONFIG_ENABLE_CHIP_SHELL
	esp_matter::console::diagnostics_register_commands();
	esp_matter::console::wifi_register_commands();
	esp_matter::console::factoryreset_register_commands();
#if CONFIG_OPENTHREAD_CLI
	esp_matter::console::otcli_register_commands();
#endif
	esp_matter::console::init();
#endif

#if CONFIG_PM_PROFILING && CONFIG_ESP_TIMER_PROFILING
	esp_timer_create_args_t timer_args = {
	    .callback = power_management_debug,
	};
	esp_timer_handle_t timer_handle;
	if (ESP_OK != esp_timer_create(&timer_args, &timer_handle)) {
	     ESP_LOGE(__func__, "Failed to create debug task");
	     abort();
	}
	if (ESP_OK != esp_timer_start_periodic(timer_handle, /* 60s in us */ 60000000)) {
	     ESP_LOGE(__func__, "Failed to start debug timer");
	     abort();
	}
#endif
}
