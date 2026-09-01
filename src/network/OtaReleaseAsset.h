#pragma once

#include <cstring>

namespace OtaReleaseAsset {

inline bool startsWith(const char* value, const char* prefix) {
  if (value == nullptr || prefix == nullptr) return false;
  const size_t prefixLength = strlen(prefix);
  return strncmp(value, prefix, prefixLength) == 0;
}

inline bool endsWith(const char* value, const char* suffix) {
  if (value == nullptr || suffix == nullptr) return false;
  const size_t valueLength = strlen(value);
  const size_t suffixLength = strlen(suffix);
  if (suffixLength > valueLength) return false;
  return strcmp(value + valueLength - suffixLength, suffix) == 0;
}

inline bool matchesYacpFirmware(const char* assetName, const char* variantSuffix) {
  return startsWith(assetName, "YACP-") && endsWith(assetName, variantSuffix);
}

}  // namespace OtaReleaseAsset
