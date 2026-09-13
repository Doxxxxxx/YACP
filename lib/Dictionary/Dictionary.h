#pragma once

#include <cstddef>
#include <cstdint>

// Minimal, on-demand StarDict reader for YACP. The object owns no file handle
// and no heap allocation. All discovery and lookup I/O happens only after the
// reader's Dictionary action is selected.
class Dictionary {
 public:
  enum class DefinitionFormat : uint8_t { Plain, Html, Typed };

  enum class LookupResult : uint8_t {
    Found,
    NotFound,
    Unavailable,
    Unsupported,
    ReadError,
  };

  struct Hit {
    uint32_t offset = 0;
    uint32_t size = 0;
    DefinitionFormat format = DefinitionFormat::Plain;
    char headword[128] = {};
  };

  // Finds the first prepared dictionary under /.dictionaries or
  // /dictionaries. A prepared dictionary has matching .ifo, .idx, .dict and
  // .idx.oft.cspt files. Discovery scans the SD card only when called.
  bool discover();

  // Resolves one word to a .dict slice. The .cspt handle is closed before the
  // .idx handle is opened, which keeps the X3 SdFat path single-reader safe.
  LookupResult locate(const char* word, Hit& hit) const;

  // Reads the located definition into caller-owned storage. The .idx file is
  // already closed before this opens .dict. The buffer is always NUL-terminated.
  bool readDefinition(Hit& hit, char* buffer, size_t capacity, size_t& length, bool& truncated) const;

  bool isOpen() const { return basePath_[0] != '\0'; }

 private:
  static constexpr size_t PATH_CAPACITY = 256;
  struct DiscoveryScratch;

  bool discoverInRoot(const char* root, DiscoveryScratch& scratch);
  bool tryDictionaryFolder(const char* folder, DiscoveryScratch& scratch);
  bool parseIfo(const char* basePath, DefinitionFormat& format, DiscoveryScratch& scratch) const;
  bool resolveCsptBounds(const char* word, uint32_t& startByte, uint32_t& endByte) const;
  bool buildPath(char* output, size_t capacity, const char* suffix) const;

  char basePath_[PATH_CAPACITY] = {};
  mutable char pathScratch_[PATH_CAPACITY] = {};
  DefinitionFormat definitionFormat_ = DefinitionFormat::Typed;
};
