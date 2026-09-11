#include "HalBootValidation.h"

#include <Logging.h>
#include <esp_ota_ops.h>

namespace {
bool awaitingValidation = false;
}

// Arduino otherwise accepts the image before setup() initializes our hardware.
extern "C" bool verifyRollbackLater() { return true; }

bool HalBootValidation::begin() {
  awaitingValidation = false;
  const auto* running = esp_ota_get_running_partition();
  if (!running) {
    LOG_ERR("BOOT", "Cannot inspect running OTA partition");
    return false;
  }
  esp_ota_img_states_t state;
  const auto result = esp_ota_get_state_partition(running, &state);
  if (result != ESP_OK) {
    LOG_INF("BOOT", "OTA validation state unavailable (%d); rollback not established", result);
    return false;
  }
  awaitingValidation = state == ESP_OTA_IMG_NEW || state == ESP_OTA_IMG_PENDING_VERIFY;
  if (awaitingValidation) {
    LOG_INF("BOOT", "Deferring image acceptance until Home renders (state=%d)", static_cast<int>(state));
  }
  return awaitingValidation;
}

bool HalBootValidation::confirmHealthyBoot() {
  if (!awaitingValidation) return true;
  const auto result = esp_ota_mark_app_valid_cancel_rollback();
  if (result != ESP_OK) {
    LOG_ERR("BOOT", "Image acceptance failed (%d); leaving validation pending", result);
    return false;
  }
  awaitingValidation = false;
  LOG_INF("BOOT", "Home rendered; OTA image accepted");
  return true;
}
