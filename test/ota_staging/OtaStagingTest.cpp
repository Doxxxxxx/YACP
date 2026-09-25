#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "mbedtls/sha256.h"
#include "network/FirmwareFlasher.h"
#include "network/HttpDownloader.h"
#include "network/OtaUpdater.h"

namespace {
struct State {
  std::string manifest;
  size_t size = 8192;
  size_t hashed = 0;
  bool fileExists = false;
  bool readerOpen = false;
  bool networkOpen = false;
  bool directoryOk = true;
  bool removeOk = true;
  bool readOk = true;
  bool openOk = true;
  bool partitionOk = true;
  uint8_t digestByte = 0;
  int hashError = 0;
  int downloads = 0;
  int flashes = 0;
  HttpDownloader::Transport transport = HttpDownloader::Transport::ESP_HTTP;
  bool cancelAfterDownload = false;
  bool cancelDuringHash = false;
  std::atomic<bool>* cancel = nullptr;
  HttpDownloader::DownloadError transfer = HttpDownloader::OK;
  firmware_flash::Result flash = firmware_flash::Result::OK;
} state;
esp_partition_t partition{6553600};
esp_http_client_config_t config;

class OtaStagingTest : public testing::Test {
 protected:
  OtaUpdater updater;
  std::atomic<bool> cancel{false};
  std::vector<int> progress;
  void SetUp() override {
    state = State{};
    state.cancel = &cancel;
  }
  void manifest(size_t size = 8192, std::string digest = std::string(64, '0'), const char* scheme = "https") {
    state.manifest =
        "{\"tag_name\":\"v1.7.2-yacp\",\"assets\":[{\"name\":\"YACP-1.7.2-yacp-tiny.bin\","
        "\"size\":" +
        std::to_string(size) + ",\"sha256\":\"" + digest + "\",\"browser_download_url\":\"" + scheme +
        "://example.test/firmware.bin\"}]}";
    ASSERT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
  }
  OtaUpdater::OtaUpdaterError install() {
    return updater.installUpdate(
        [](void* context) {
          auto* self = static_cast<OtaStagingTest*>(context);
          self->progress.push_back(
              static_cast<int>(self->updater.getProcessedSize() * 100 / self->updater.getTotalSize()));
        },
        this, &cancel);
  }
  void noFlash() {
    EXPECT_EQ(state.flashes, 0);
    EXPECT_FALSE(state.readerOpen);
    EXPECT_FALSE(state.networkOpen);
    EXPECT_FALSE(state.fileExists);
  }
};

TEST_F(OtaStagingTest, ClosesNetworkAndReaderBeforeValidatedSdInstall) {
  manifest();
  ASSERT_EQ(install(), OtaUpdater::OK);
  EXPECT_EQ(state.transport, HttpDownloader::Transport::WOLFSSL);
  EXPECT_EQ(state.flashes, 1);
  EXPECT_EQ(state.hashed, state.size);
  EXPECT_FALSE(state.fileExists);
  ASSERT_FALSE(progress.empty());
  EXPECT_EQ(progress.front(), 0);
  EXPECT_EQ(progress.back(), 100);
  EXPECT_TRUE(std::is_sorted(progress.begin(), progress.end()));
}
TEST_F(OtaStagingTest, TruncatedTransferNeverFlashes) {
  manifest();
  state.transfer = HttpDownloader::HTTP_ERROR;
  EXPECT_EQ(install(), OtaUpdater::HTTP_ERROR);
  noFlash();
}
TEST_F(OtaStagingTest, FullSdCardNeverFlashes) {
  manifest();
  state.transfer = HttpDownloader::FILE_ERROR;
  EXPECT_EQ(install(), OtaUpdater::INTERNAL_UPDATE_ERROR);
  noFlash();
}
TEST_F(OtaStagingTest, WrongManifestDigestNeverFlashes) {
  manifest();
  state.digestByte = 1;
  EXPECT_EQ(install(), OtaUpdater::HASH_MISMATCH_ERROR);
  noFlash();
}
TEST_F(OtaStagingTest, WrongManifestSizeNeverFlashes) {
  manifest(8193);
  EXPECT_EQ(install(), OtaUpdater::INTERNAL_UPDATE_ERROR);
  noFlash();
}
TEST_F(OtaStagingTest, InvalidDigestRejectedBeforeDownload) {
  manifest(8192, "abcd");
  EXPECT_EQ(install(), OtaUpdater::JSON_PARSE_ERROR);
  EXPECT_EQ(state.downloads, 0);
  noFlash();
}
TEST_F(OtaStagingTest, HttpRequiresManifestDigest) {
  manifest(8192, "", "http");
  EXPECT_EQ(install(), OtaUpdater::JSON_PARSE_ERROR);
  EXPECT_EQ(state.downloads, 0);
  noFlash();
}
TEST_F(OtaStagingTest, HttpsWithoutDigestStillUsesImageValidation) {
  manifest(8192, "");
  EXPECT_EQ(install(), OtaUpdater::OK);
  EXPECT_EQ(state.transport, HttpDownloader::Transport::ESP_HTTP);
  EXPECT_EQ(state.hashed, 0);
  EXPECT_EQ(state.flashes, 1);
}
TEST_F(OtaStagingTest, UnknownSizeKeepsProgressMonotonic) {
  manifest(0);
  EXPECT_EQ(install(), OtaUpdater::OK);
  EXPECT_TRUE(std::is_sorted(progress.begin(), progress.end()));
  EXPECT_EQ(progress.back(), 100);
}
TEST_F(OtaStagingTest, CancelledTransferNeverFlashes) {
  manifest();
  state.transfer = HttpDownloader::ABORTED;
  EXPECT_EQ(install(), OtaUpdater::CANCELLED_ERROR);
  noFlash();
}
TEST_F(OtaStagingTest, CancellationAfterDownloadNeverFlashes) {
  manifest();
  state.cancelAfterDownload = true;
  EXPECT_EQ(install(), OtaUpdater::CANCELLED_ERROR);
  noFlash();
}
TEST_F(OtaStagingTest, CancellationDuringHashNeverFlashes) {
  manifest();
  state.cancelDuringHash = true;
  EXPECT_EQ(install(), OtaUpdater::CANCELLED_ERROR);
  noFlash();
}
TEST_F(OtaStagingTest, ReadFailureNeverFlashes) {
  manifest();
  state.readOk = false;
  EXPECT_EQ(install(), OtaUpdater::INTERNAL_UPDATE_ERROR);
  noFlash();
}
TEST_F(OtaStagingTest, HashEngineFailureNeverFlashes) {
  manifest();
  state.hashError = -1;
  EXPECT_EQ(install(), OtaUpdater::INTERNAL_UPDATE_ERROR);
  noFlash();
}
TEST_F(OtaStagingTest, StagedOpenFailureNeverFlashes) {
  manifest();
  state.openOk = false;
  EXPECT_EQ(install(), OtaUpdater::INTERNAL_UPDATE_ERROR);
  noFlash();
}
TEST_F(OtaStagingTest, MissingPartitionRejectedBeforeDownload) {
  manifest();
  state.partitionOk = false;
  EXPECT_EQ(install(), OtaUpdater::INTERNAL_UPDATE_ERROR);
  EXPECT_EQ(state.downloads, 0);
  noFlash();
}
TEST_F(OtaStagingTest, OversizedManifestRejectedBeforeDownload) {
  manifest(partition.size + 1);
  EXPECT_EQ(install(), OtaUpdater::INTERNAL_UPDATE_ERROR);
  EXPECT_EQ(state.downloads, 0);
  noFlash();
}
TEST_F(OtaStagingTest, OversizedDownloadWithoutManifestSizeNeverFlashes) {
  manifest(0);
  state.size = partition.size + 1;
  EXPECT_EQ(install(), OtaUpdater::INTERNAL_UPDATE_ERROR);
  noFlash();
}
TEST_F(OtaStagingTest, PreviousPartialIsRemovedBeforeDownload) {
  manifest();
  state.fileExists = true;
  EXPECT_EQ(install(), OtaUpdater::OK);
  EXPECT_FALSE(state.fileExists);
}
TEST_F(OtaStagingTest, UnremovablePartialStopsBeforeDownload) {
  manifest();
  state.fileExists = true;
  state.removeOk = false;
  EXPECT_EQ(install(), OtaUpdater::INTERNAL_UPDATE_ERROR);
  EXPECT_EQ(state.downloads, 0);
  EXPECT_EQ(state.flashes, 0);
}
TEST_F(OtaStagingTest, ImageValidationFailurePropagates) {
  manifest();
  state.flash = firmware_flash::Result::BAD_SHA;
  EXPECT_EQ(install(), OtaUpdater::INTERNAL_UPDATE_ERROR);
  EXPECT_FALSE(state.fileExists);
  EXPECT_FALSE(state.readerOpen);
}
TEST_F(OtaStagingTest, FlashAllocationFailurePropagates) {
  manifest();
  state.flash = firmware_flash::Result::OOM;
  EXPECT_EQ(install(), OtaUpdater::OOM_ERROR);
  EXPECT_FALSE(state.fileExists);
}
}  // namespace

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* input) {
  config = *input;
  return &config;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char*, const char*) { return ESP_OK; }
esp_err_t esp_http_client_perform(esp_http_client_handle_t client) {
  esp_http_client_event_t event{HTTP_EVENT_ON_DATA, client->user_data, static_cast<int>(state.manifest.size()),
                                state.manifest.data()};
  return client->event_handler(&event);
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t) { return ESP_OK; }
extern "C" esp_err_t esp_crt_bundle_attach(void*) { return ESP_OK; }
const esp_partition_t* esp_ota_get_next_update_partition(const void*) {
  return state.partitionOk ? &partition : nullptr;
}
bool TestStorage::ensureDirectoryExists(const char*) { return state.directoryOk; }
bool TestStorage::exists(const char*) { return state.fileExists; }
bool TestStorage::remove(const char*) {
  EXPECT_FALSE(state.readerOpen);
  if (!state.removeOk) return false;
  state.fileExists = false;
  return true;
}
bool TestStorage::openFileForRead(const char*, const char*, HalFile& file) {
  EXPECT_FALSE(state.networkOpen);
  EXPECT_FALSE(state.readerOpen);
  if (!state.openOk) return false;
  state.readerOpen = file.opened = true;
  return true;
}
size_t HalFile::fileSize() { return state.size; }
int HalFile::read(void* data, size_t count) {
  if (!state.readOk) return -1;
  std::memset(data, 0, count);
  offset += count;
  return static_cast<int>(count);
}
void HalFile::close() { state.readerOpen = opened = false; }
void mbedtls_sha256_init(mbedtls_sha256_context*) {}
void mbedtls_sha256_free(mbedtls_sha256_context*) {}
int mbedtls_sha256_starts(mbedtls_sha256_context*, int) { return state.hashError; }
int mbedtls_sha256_update(mbedtls_sha256_context*, const uint8_t*, size_t size) {
  state.hashed += size;
  if (state.cancelDuringHash) state.cancel->store(true);
  return 0;
}
int mbedtls_sha256_finish(mbedtls_sha256_context*, uint8_t* result) {
  std::memset(result, state.digestByte, 32);
  return 0;
}
HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string&, const std::string&,
                                                             ProgressCallback progress, bool*, const std::string&,
                                                             const std::string&, DownloadOptions options) {
  ++state.downloads;
  state.transport = options.transport;
  EXPECT_FALSE(state.fileExists);
  EXPECT_FALSE(options.resumePartial);
  EXPECT_FALSE(options.preservePartial);
  state.networkOpen = true;
  state.fileExists = true;
  progress(state.size / 2, state.size);
  progress(state.size, state.size);
  state.networkOpen = false;
  if (state.cancelAfterDownload) state.cancel->store(true);
  return state.transfer;
}
firmware_flash::Result firmware_flash::flashFromSdPath(const char*, ProgressCb progress, void* ctx,
                                                       bool alreadyValidated) {
  ++state.flashes;
  EXPECT_FALSE(state.networkOpen);
  EXPECT_FALSE(state.readerOpen);
  EXPECT_FALSE(alreadyValidated);  // Must keep the installer's real image checks.
  EXPECT_TRUE(state.fileExists);
  if (state.flash == Result::OK) {
    progress(state.size / 2, state.size, ctx);
    progress(state.size, state.size, ctx);
  }
  return state.flash;
}
const char* firmware_flash::resultName(Result) { return "test result"; }
