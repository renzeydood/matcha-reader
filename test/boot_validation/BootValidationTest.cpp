#include <HalBootValidation.h>
#include <esp_ota_ops.h>
#include <gtest/gtest.h>

extern "C" bool verifyRollbackLater();
namespace {
esp_partition_t partition{};
bool hasPartition;
esp_ota_img_states_t imageState;
esp_err_t readResult, acceptResult;
int acceptCalls;
class BootValidationTest : public ::testing::Test {
  void SetUp() override {
    hasPartition = true;
    imageState = ESP_OTA_IMG_VALID;
    readResult = acceptResult = ESP_OK;
    acceptCalls = 0;
    HalBootValidation::begin();
  }
};
}  // namespace
const esp_partition_t* esp_ota_get_running_partition() { return hasPartition ? &partition : nullptr; }
esp_err_t esp_ota_get_state_partition(const esp_partition_t*, esp_ota_img_states_t* state) {
  *state = imageState;
  return readResult;
}
esp_err_t esp_ota_mark_app_valid_cancel_rollback() {
  ++acceptCalls;
  return acceptResult;
}

TEST_F(BootValidationTest, ArduinoCannotAcceptBeforeSetup) {
  EXPECT_TRUE(verifyRollbackLater());
  EXPECT_EQ(acceptCalls, 0);
}
TEST_F(BootValidationTest, PendingBootRemainsUnacceptedUntilCheckpoint) {
  imageState = ESP_OTA_IMG_PENDING_VERIFY;
  EXPECT_TRUE(HalBootValidation::begin());
  EXPECT_EQ(acceptCalls, 0);
  EXPECT_TRUE(HalBootValidation::confirmHealthyBoot());
  EXPECT_EQ(acceptCalls, 1);
  EXPECT_TRUE(HalBootValidation::confirmHealthyBoot());
  EXPECT_EQ(acceptCalls, 1);
}
TEST_F(BootValidationTest, NewStateAlsoWaitsForCheckpoint) {
  imageState = ESP_OTA_IMG_NEW;
  EXPECT_TRUE(HalBootValidation::begin());
  EXPECT_EQ(acceptCalls, 0);
  EXPECT_TRUE(HalBootValidation::confirmHealthyBoot());
  EXPECT_EQ(acceptCalls, 1);
}
TEST_F(BootValidationTest, FailedAcceptanceRemainsPending) {
  imageState = ESP_OTA_IMG_PENDING_VERIFY;
  ASSERT_TRUE(HalBootValidation::begin());
  acceptResult = -1;
  EXPECT_FALSE(HalBootValidation::confirmHealthyBoot());
  acceptResult = ESP_OK;
  EXPECT_TRUE(HalBootValidation::confirmHealthyBoot());
  EXPECT_EQ(acceptCalls, 2);
}
TEST_F(BootValidationTest, DoesNotOverwriteValidOrRejectedImages) {
  for (auto state : {ESP_OTA_IMG_VALID, ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED, ESP_OTA_IMG_UNDEFINED}) {
    imageState = state;
    EXPECT_FALSE(HalBootValidation::begin());
    EXPECT_TRUE(HalBootValidation::confirmHealthyBoot());
  }
  EXPECT_EQ(acceptCalls, 0);
}
TEST_F(BootValidationTest, MissingPartitionDoesNotWriteOtadata) {
  hasPartition = false;
  EXPECT_FALSE(HalBootValidation::begin());
  EXPECT_TRUE(HalBootValidation::confirmHealthyBoot());
  EXPECT_EQ(acceptCalls, 0);
}
TEST_F(BootValidationTest, UnreadableStateDoesNotWriteOtadata) {
  imageState = ESP_OTA_IMG_PENDING_VERIFY;
  readResult = -1;
  EXPECT_FALSE(HalBootValidation::begin());
  EXPECT_TRUE(HalBootValidation::confirmHealthyBoot());
  EXPECT_EQ(acceptCalls, 0);
}
