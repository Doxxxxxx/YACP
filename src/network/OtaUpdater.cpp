#ifdef SIMULATOR
#include "OtaUpdater.h"

bool OtaUpdater::isUpdateNewer() const { return false; }
const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }
OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() { return NO_UPDATE; }
OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback, void*, std::atomic<bool>*) { return NO_UPDATE; }
#else
#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <ReleaseJsonParser.h>

#include <algorithm>
#include <cstring>

#include "AppVersion.h"
#include "OtaUpdater.h"
#include "FirmwareFlasher.h"
#include "HttpDownloader.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "mbedtls/sha256.h"
#include "network/OtaReleaseAsset.h"
#include "network/WifiPowerSaveGuard.h"

namespace {
#ifndef CROSSINK_OTA_RELEASE_URL
#define CROSSINK_OTA_RELEASE_URL "https://api.github.com/repos/Sichroteph/YACP/releases/latest"
#endif

constexpr char latestReleaseUrl[] = CROSSINK_OTA_RELEASE_URL;

#ifdef CROSSPOINT_FIRMWARE_VARIANT
constexpr char firmwareAssetSuffix[] = "-" CROSSPOINT_FIRMWARE_VARIANT ".bin";
#else
constexpr char firmwareAssetSuffix[] = ".bin";
#endif

constexpr size_t VERSION_SEGMENT_COUNT = 4;
constexpr size_t OTA_PROGRESS_UPDATE_BYTES = 64 * 1024;
constexpr size_t OTA_HASH_CHUNK = 1024;
constexpr char OTA_STAGE_DIR[] = "/.crosspoint";
constexpr char OTA_STAGE_PATH[] = "/.crosspoint/ota-update.bin";

struct ParsedVersion {
  int segments[VERSION_SEGMENT_COUNT] = {0, 0, 0, 0};
  bool valid = false;
  bool releaseCandidate = false;
};

bool isDigit(const char c) { return c >= '0' && c <= '9'; }

bool startsWithNumberAfterOptionalV(const char* version) {
  if (version == nullptr) return false;
  if ((version[0] == 'v' || version[0] == 'V') && isDigit(version[1])) return true;
  return isDigit(version[0]);
}

bool containsRcMarker(const char* version) {
  if (version == nullptr) return false;
  for (const char* p = version; p[0] != '\0' && p[1] != '\0' && p[2] != '\0'; ++p) {
    if (p[0] == '-' && (p[1] == 'r' || p[1] == 'R') && (p[2] == 'c' || p[2] == 'C')) {
      return true;
    }
  }
  return false;
}

ParsedVersion parseVersion(const char* version) {
  ParsedVersion parsed;
  if (!startsWithNumberAfterOptionalV(version)) return parsed;

  const char* p = version;
  if (p[0] == 'v' || p[0] == 'V') ++p;

  size_t segmentIndex = 0;
  while (segmentIndex < VERSION_SEGMENT_COUNT) {
    if (!isDigit(*p)) return parsed;

    int value = 0;
    while (isDigit(*p)) {
      value = value * 10 + (*p - '0');
      ++p;
    }
    parsed.segments[segmentIndex] = value;
    ++segmentIndex;

    if (*p != '.') break;
    ++p;
  }

  parsed.valid = true;
  parsed.releaseCandidate = containsRcMarker(version);
  return parsed;
}

int compareVersions(const char* latestVersion, const char* currentVersion) {
  const ParsedVersion latest = parseVersion(latestVersion);
  const ParsedVersion current = parseVersion(currentVersion);
  if (!latest.valid || !current.valid) return 0;

  for (size_t i = 0; i < VERSION_SEGMENT_COUNT; ++i) {
    if (latest.segments[i] != current.segments[i]) {
      return latest.segments[i] > current.segments[i] ? 1 : -1;
    }
  }

  if (current.releaseCandidate && !latest.releaseCandidate) return 1;
  return 0;
}

char lowerHex(const uint8_t value) {
  return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
}

char asciiLower(const char c) { return (c >= 'A' && c <= 'F') ? static_cast<char>(c - 'A' + 'a') : c; }

bool isHexChar(const char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

bool isSha256Hex(const char* value) {
  if (value == nullptr || strlen(value) != 64) return false;
  for (size_t i = 0; i < 64; ++i) {
    if (!isHexChar(value[i])) return false;
  }
  return value[64] == '\0';
}

bool sha256Matches(const uint8_t digest[32], const char* expectedHex) {
  if (!isSha256Hex(expectedHex)) return false;

  for (size_t i = 0; i < 32; ++i) {
    const char high = lowerHex((digest[i] >> 4) & 0x0F);
    const char low = lowerHex(digest[i] & 0x0F);
    if (high != asciiLower(expectedHex[i * 2]) || low != asciiLower(expectedHex[i * 2 + 1])) return false;
  }
  return true;
}

void formatSha256(const uint8_t digest[32], char output[65]) {
  for (size_t i = 0; i < 32; ++i) {
    output[i * 2] = lowerHex((digest[i] >> 4) & 0x0F);
    output[i * 2 + 1] = lowerHex(digest[i] & 0x0F);
  }
  output[64] = '\0';
}

bool isHttpUrl(const std::string& url) { return url.rfind("http://", 0) == 0; }

bool isMatchingFirmwareAssetName(const char* assetName) {
  return OtaReleaseAsset::matchesYacpFirmware(assetName, firmwareAssetSuffix);
}

/*
 * When esp_crt_bundle.h included, it is pointing wrong header file
 * which is something under WifiClientSecure because of our framework based on arduno platform.
 * To manage this obstacle, don't include anything, just extern and it will point correct one.
 */
extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

size_t totalBytesReceived = 0;

struct OtaInstallContext {
  size_t* processedSize = nullptr;
  size_t totalSize = 0;
  size_t lastProgressBytes = 0;
  int lastReportedPct = -1;
  OtaUpdater::ProgressCallback onProgress = nullptr;
  void* progressCtx = nullptr;
};

esp_err_t release_manifest_event_handler(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
  if (event->data_len <= 0) return ESP_OK;

  auto* parser = static_cast<ReleaseJsonParser*>(event->user_data);
  if (parser == nullptr) {
    LOG_ERR("OTA", "HTTP client parser missing");
    return ESP_ERR_INVALID_ARG;
  }

  totalBytesReceived += static_cast<size_t>(event->data_len);
  LOG_DBG("OTA", "HTTP chunk: %d bytes (total: %zu)", event->data_len, totalBytesReceived);
  parser->feed(static_cast<const char*>(event->data), event->data_len);
  return ESP_OK;
}

void notifyOtaProgress(OtaInstallContext* ctx, const bool force) {
  if (ctx == nullptr || ctx->onProgress == nullptr || ctx->processedSize == nullptr || ctx->totalSize == 0) return;

  const size_t processed = *ctx->processedSize;
  const int pct = static_cast<int>(static_cast<uint64_t>(processed) * 100 / ctx->totalSize);
  if (force || pct != ctx->lastReportedPct || processed - ctx->lastProgressBytes >= OTA_PROGRESS_UPDATE_BYTES) {
    ctx->lastReportedPct = pct;
    ctx->lastProgressBytes = processed;
    ctx->onProgress(ctx->progressCtx);
  }
}

// The SDK download buffer has already been freed when this helper runs.
// Use one fallible 1 KiB heap buffer: too large for the task stack, and no
// permanent static RAM cost while reading books. Release it before flashing.
OtaUpdater::OtaUpdaterError verifyStagedHash(HalFile& file, const char* expectedHex,
                                          std::atomic<bool>* cancelRequested) {
  auto buffer = makeUniqueNoThrow<uint8_t[]>(OTA_HASH_CHUNK);
  if (!buffer) {
    LOG_ERR("OTA", "Failed to allocate %zu-byte hash buffer", OTA_HASH_CHUNK);
    return OtaUpdater::OOM_ERROR;
  }
  mbedtls_sha256_context shaCtx;
  mbedtls_sha256_init(&shaCtx);
  ScopedCleanup hashCleanup{[&shaCtx] { mbedtls_sha256_free(&shaCtx); }};
  if (mbedtls_sha256_starts(&shaCtx, 0) != 0) {
    LOG_ERR("OTA", "Failed to initialize staged firmware hash");
    return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }
  size_t remaining = file.fileSize();
  while (remaining > 0) {
    if (cancelRequested != nullptr && cancelRequested->load(std::memory_order_relaxed)) {
      LOG_INF("OTA", "Update cancelled during hash verification");
      return OtaUpdater::CANCELLED_ERROR;
    }
    const size_t want = std::min(remaining, OTA_HASH_CHUNK);
    if (file.read(buffer.get(), want) != static_cast<int>(want)) {
      LOG_ERR("OTA", "Staged firmware read failed (%zu bytes remaining)", remaining);
      return OtaUpdater::INTERNAL_UPDATE_ERROR;
    }
    if (mbedtls_sha256_update(&shaCtx, buffer.get(), want) != 0) {
      LOG_ERR("OTA", "Failed to hash staged firmware");
      return OtaUpdater::INTERNAL_UPDATE_ERROR;
    }
    remaining -= want;
    esp_task_wdt_reset();
    delay(1);
  }
  uint8_t digest[32];
  if (mbedtls_sha256_finish(&shaCtx, digest) != 0) {
    LOG_ERR("OTA", "Failed to finish staged firmware hash");
    return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }
  if (!sha256Matches(digest, expectedHex)) {
    char actual[65];
    formatSha256(digest, actual);
    LOG_ERR("OTA", "Staged firmware sha256 mismatch: expected=%s actual=%s", expectedHex, actual);
    return OtaUpdater::HASH_MISMATCH_ERROR;
  }
  LOG_INF("OTA", "Staged firmware sha256 verified");
  return OtaUpdater::OK;
}
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  WifiPowerSaveGuard wifiPowerSaveGuard;

  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  otaSha256.clear();
  otaSize = 0;
  processedSize = 0;
  totalSize = 0;

  esp_err_t esp_err;
  ReleaseJsonParser releaseParser(isMatchingFirmwareAssetName);

  esp_http_client_config_t client_config = {
      .url = latestReleaseUrl,
      .event_handler = release_manifest_event_handler,
      // 4096 holds the API response headers; the 32KB body streams through the
      // parser in chunks so RX needn't be larger. TX only carries our GET.
      // Both free before installUpdate, so smaller leaves it less fragmentation.
      .buffer_size = 4096,
      .buffer_size_tx = 1024,
      .user_data = &releaseParser,
      .skip_cert_common_name_check = true,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .keep_alive_enable = true,
  };

  totalBytesReceived = 0;
  LOG_DBG("OTA", "Checking for update (current: %s)", CROSSINK_VERSION);

  esp_http_client_handle_t client_handle = esp_http_client_init(&client_config);
  if (!client_handle) {
    LOG_ERR("OTA", "HTTP Client Handle Failed");
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_http_client_set_header(client_handle, "User-Agent", "CrossInk-ESP32-" CROSSINK_VERSION);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_set_header Failed : %s", esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_http_client_perform(client_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_perform Failed : %s", esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return HTTP_ERROR;
  }

  esp_err = esp_http_client_cleanup(client_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_cleanup Failed : %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_DBG("OTA", "Response received: %zu bytes total", totalBytesReceived);
  LOG_DBG("OTA", "Parser results: tag=%s firmware=%s", releaseParser.foundTag() ? "yes" : "no",
          releaseParser.foundFirmware() ? "yes" : "no");

  if (!releaseParser.foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON");
    return JSON_PARSE_ERROR;
  }

  latestVersion = releaseParser.getTagName();

  const ParsedVersion parsedLatestVersion = parseVersion(latestVersion.c_str());
  const ParsedVersion parsedCurrentVersion = parseVersion(CROSSINK_VERSION);
  if (!parsedLatestVersion.valid || !parsedCurrentVersion.valid) {
    LOG_ERR("OTA", "Invalid version in update manifest: latest=%s current=%s", latestVersion.c_str(),
            CROSSINK_VERSION);
    return JSON_PARSE_ERROR;
  }

  if (compareVersions(latestVersion.c_str(), CROSSINK_VERSION) <= 0) {
    LOG_DBG("OTA", "No newer YACP release: latest=%s current=%s", latestVersion.c_str(), CROSSINK_VERSION);
    return NO_UPDATE;
  }

  if (!releaseParser.foundFirmware()) {
    LOG_ERR("OTA", "No YACP asset ending in %s found for release %s", firmwareAssetSuffix, latestVersion.c_str());
    return JSON_PARSE_ERROR;
  }

  otaUrl = releaseParser.getFirmwareUrl();
  otaSha256 = releaseParser.getFirmwareSha256();
  otaSize = releaseParser.getFirmwareSize();
  totalSize = otaSize;
  updateAvailable = true;

  LOG_DBG("OTA", "Found update: tag=%s size=%zu sha256=%s", latestVersion.c_str(), otaSize,
          otaSha256.empty() ? "missing" : "present");
  LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSINK_VERSION) {
    return false;
  }

  const int comparison = compareVersions(latestVersion.c_str(), CROSSINK_VERSION);
  LOG_DBG("OTA", "Version comparison latest=%s current=%s result=%d", latestVersion.c_str(), CROSSINK_VERSION,
          comparison);
  return comparison > 0;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx,
                                                      std::atomic<bool>* cancelRequested) {
  const auto isCancellationRequested = [cancelRequested]() {
    return cancelRequested != nullptr && cancelRequested->load(std::memory_order_relaxed);
  };
  if (!isUpdateNewer()) return UPDATE_OLDER_ERROR;
  if (isCancellationRequested()) return CANCELLED_ERROR;

  const bool hasManifestSha256 = isSha256Hex(otaSha256.c_str());
  if ((!otaSha256.empty() && !hasManifestSha256) || (isHttpUrl(otaUrl) && !hasManifestSha256)) {
    LOG_ERR("OTA", "Missing or invalid firmware manifest sha256");
    return JSON_PARSE_ERROR;
  }
  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (updatePartition == nullptr) {
    LOG_ERR("OTA", "No OTA update partition found");
    return INTERNAL_UPDATE_ERROR;
  }
  if (otaSize > updatePartition->size) {
    LOG_ERR("OTA", "Firmware too large: %zu > %zu", otaSize, updatePartition->size);
    return INTERNAL_UPDATE_ERROR;
  }
  if (!Storage.ensureDirectoryExists(OTA_STAGE_DIR)) {
    LOG_ERR("OTA", "Failed to create OTA staging directory");
    return INTERNAL_UPDATE_ERROR;
  }
  if (Storage.exists(OTA_STAGE_PATH) && !Storage.remove(OTA_STAGE_PATH)) {
    LOG_ERR("OTA", "Failed to remove previous staged firmware");
    return INTERNAL_UPDATE_ERROR;
  }
  // The path is reserved for OTA. Clean up partial, rejected, and installed files.
  ScopedCleanup stagedCleanup{[] {
    if (Storage.exists(OTA_STAGE_PATH) && !Storage.remove(OTA_STAGE_PATH)) {
      LOG_ERR("OTA", "Failed to clean up staged firmware");
    }
  }};
  // This HAL cannot report free SD capacity. A full card is reported by the
  // downloader's checked writes, before the flash partition is touched.

  // Download occupies the first half; flashing occupies the second. For a
  // manifest without a size, the partition size bounds download progress until
  // the actual staged size is known, so download alone never displays 100%.
  const size_t stagingWork = otaSize > 0 ? otaSize : updatePartition->size;
  processedSize = 0;
  totalSize = stagingWork * 2;
  OtaInstallContext installCtx;
  installCtx.processedSize = &processedSize;
  installCtx.totalSize = totalSize;
  installCtx.onProgress = onProgress;
  installCtx.progressCtx = ctx;
  notifyOtaProgress(&installCtx, true);

  // Adapted from CrossInk v1.5.1: finish the network transfer before any flash
  // erase/write, then reuse the SD installer. The trusted manifest digest
  // authenticates the firmware streamed through wolfSSL before installation.
  HttpDownloader::DownloadOptions options;
  options.shouldCancel = isCancellationRequested;
  if (hasManifestSha256) options.transport = HttpDownloader::Transport::WOLFSSL;
  LOG_INF("OTA", "Downloading firmware to SD: size=%zu heap=%u maxAlloc=%u", otaSize, ESP.getFreeHeap(),
          ESP.getMaxAllocHeap());
  const auto transfer = HttpDownloader::downloadToFile(
      otaUrl, OTA_STAGE_PATH,
      [&](const size_t downloaded, const size_t) {
        processedSize = std::min(downloaded, stagingWork);
        notifyOtaProgress(&installCtx, false);
      },
      nullptr, "", "", std::move(options));
  if (transfer != HttpDownloader::OK) {
    LOG_ERR("OTA", "SD download failed: error=%d", static_cast<int>(transfer));
    if (transfer == HttpDownloader::ABORTED || isCancellationRequested()) return CANCELLED_ERROR;
    return transfer == HttpDownloader::FILE_ERROR ? INTERNAL_UPDATE_ERROR : HTTP_ERROR;
  }
  if (isCancellationRequested()) return CANCELLED_ERROR;

  size_t stagedSize = 0;
  {
    HalFile file;
    if (!Storage.openFileForRead("OTA", OTA_STAGE_PATH, file) || !file) {
      LOG_ERR("OTA", "Failed to open staged firmware");
      return INTERNAL_UPDATE_ERROR;
    }
    ScopedCleanup closeFile{[&file] { file.close(); }};
    stagedSize = file.fileSize();
    if (stagedSize == 0 || stagedSize > updatePartition->size || (otaSize > 0 && stagedSize != otaSize)) {
      LOG_ERR("OTA", "Staged firmware size mismatch: got=%zu manifest=%zu partition=%zu", stagedSize, otaSize,
              updatePartition->size);
      return INTERNAL_UPDATE_ERROR;
    }
    if (hasManifestSha256) {
      const auto hashResult = verifyStagedHash(file, otaSha256.c_str(), cancelRequested);
      if (hashResult != OK) return hashResult;
    }
  }  // Close the reader before the SD installer reopens the same path.
  if (isCancellationRequested()) return CANCELLED_ERROR;

  processedSize = stagedSize;
  totalSize = stagedSize * 2;
  installCtx.totalSize = totalSize;
  LOG_INF("OTA", "Download closed; validating and flashing SD image: size=%zu heap=%u maxAlloc=%u", stagedSize,
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  // The installer checks the ESP checksum, SHA trailer, and partition limit
  // before erasing. Once flashing starts, finish it rather than cancelling.
  const auto result = firmware_flash::flashFromSdPath(
      OTA_STAGE_PATH,
      [](const size_t written, const size_t total, void* context) {
        auto* install = static_cast<OtaInstallContext*>(context);
        *install->processedSize = total + written;
        notifyOtaProgress(install, false);
      },
      &installCtx);
  if (result != firmware_flash::Result::OK) {
    LOG_ERR("OTA", "SD firmware install failed: %s", firmware_flash::resultName(result));
    return result == firmware_flash::Result::OOM ? OOM_ERROR : INTERNAL_UPDATE_ERROR;
  }
  notifyOtaProgress(&installCtx, true);
  LOG_INF("OTA", "Update completed: %zu-byte firmware", stagedSize);
  return OK;
}
#endif
