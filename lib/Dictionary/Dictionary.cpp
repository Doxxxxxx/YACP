#include "Dictionary.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

constexpr char DICTIONARY_ROOTS[][16] = {"/.dictionaries", "/dictionaries"};
constexpr size_t NAME_CAPACITY = 128;
constexpr size_t MAX_CANDIDATE_FOLDERS = 16;
constexpr size_t CANDIDATE_NAME_CAPACITY = 96;

constexpr uint8_t CSPT_MAGIC[4] = {'C', 'S', 'P', 'T'};
constexpr uint8_t CSPT_VERSION = 1;
constexpr size_t CSPT_HEADER_SIZE = 12;
constexpr size_t MAX_CSPT_PREFIX_BYTES = 64;

uint16_t readLe16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t readLe32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

uint32_t readBe32(const uint8_t* bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

unsigned char foldAscii(const unsigned char value) {
  return value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
}

int compareFolded(const char* left, const char* right) {
  while (*left != '\0' && *right != '\0') {
    const unsigned char l = foldAscii(static_cast<unsigned char>(*left));
    const unsigned char r = foldAscii(static_cast<unsigned char>(*right));
    if (l != r) return l < r ? -1 : 1;
    left++;
    right++;
  }
  if (*left == *right) return 0;
  return *left == '\0' ? -1 : 1;
}

int comparePrefixToWord(const uint8_t* prefix, const size_t prefixLen, const char* word) {
  for (size_t i = 0; i < prefixLen; i++) {
    if (prefix[i] == 0) return word[i] == '\0' ? 0 : -1;
    if (word[i] == '\0') return 1;
    const unsigned char l = foldAscii(prefix[i]);
    const unsigned char r = foldAscii(static_cast<unsigned char>(word[i]));
    if (l != r) return l < r ? -1 : 1;
  }
  return word[prefixLen] == '\0' ? 0 : -1;
}

bool endsWithIgnoreCase(const char* value, const char* suffix) {
  const size_t valueLen = strlen(value);
  const size_t suffixLen = strlen(suffix);
  if (valueLen < suffixLen) return false;
  return compareFolded(value + valueLen - suffixLen, suffix) == 0;
}

bool composePath(char* output, const size_t capacity, const char* base, const char* suffix) {
  const int count = snprintf(output, capacity, "%s%s", base, suffix);
  return count > 0 && static_cast<size_t>(count) < capacity;
}

bool isCommonUtf8QuoteAt(const char* text, const size_t pos, const size_t length) {
  if (pos + 2 >= length) return false;
  const auto* bytes = reinterpret_cast<const uint8_t*>(text + pos);
  return bytes[0] == 0xE2 && bytes[1] == 0x80 &&
         (bytes[2] == 0x98 || bytes[2] == 0x99 || bytes[2] == 0x9C || bytes[2] == 0x9D);
}

bool cleanWord(const char* input, char* output, const size_t capacity) {
  if (!input || capacity < 2) return false;
  const size_t inputLen = strnlen(input, capacity * 2);
  size_t start = 0;
  size_t end = inputLen;

  while (start < end) {
    const unsigned char c = static_cast<unsigned char>(input[start]);
    if (c < 0x80 && !std::isalnum(c)) {
      start++;
    } else if (isCommonUtf8QuoteAt(input, start, end)) {
      start += 3;
    } else {
      break;
    }
  }
  while (end > start) {
    const unsigned char c = static_cast<unsigned char>(input[end - 1]);
    if (c < 0x80 && !std::isalnum(c)) {
      end--;
    } else if (end >= start + 3 && isCommonUtf8QuoteAt(input, end - 3, end)) {
      end -= 3;
    } else {
      break;
    }
  }

  const size_t length = end - start;
  if (length == 0 || length >= capacity) return false;
  memcpy(output, input + start, length);
  output[length] = '\0';
  return true;
}

bool readIdxEntry(HalFile& idx, char* headword, const size_t headwordCapacity, bool& headwordTruncated,
                  uint32_t& dictOffset, uint32_t& dictSize) {
  size_t length = 0;
  headwordTruncated = false;
  while (true) {
    const int value = idx.read();
    if (value < 0) return false;
    if (value == 0) break;
    if (length + 1 < headwordCapacity) {
      headword[length++] = static_cast<char>(value);
    } else {
      headwordTruncated = true;
    }
  }
  headword[length] = '\0';

  uint8_t location[8];
  if (idx.read(location, sizeof(location)) != static_cast<int>(sizeof(location))) return false;
  dictOffset = readBe32(location);
  dictSize = readBe32(location + 4);
  return true;
}

}  // namespace

struct Dictionary::DiscoveryScratch {
  char candidates[MAX_CANDIDATE_FOLDERS * CANDIDATE_NAME_CAPACITY] = {};
  char name[NAME_CAPACITY] = {};
  char stem[NAME_CAPACITY] = {};
  char base[PATH_CAPACITY] = {};
  char path[PATH_CAPACITY] = {};
  char line[192] = {};
};

bool Dictionary::buildPath(char* output, const size_t capacity, const char* suffix) const {
  if (!isOpen() || !composePath(output, capacity, basePath_, suffix)) {
    LOG_ERR("DICT", "Dictionary path is too long");
    return false;
  }
  return true;
}

bool Dictionary::parseIfo(const char* basePath, DefinitionFormat& format, DiscoveryScratch& scratch) const {
  if (!composePath(scratch.path, sizeof(scratch.path), basePath, ".ifo")) return false;

  HalFile file;
  if (!Storage.openFileForRead("DICT", scratch.path, file)) return false;

  format = DefinitionFormat::Typed;
  bool unsupportedOffsets = false;
  bool unsupportedTypes = false;
  size_t lineLen = 0;
  while (true) {
    const int value = file.read();
    if (value < 0 || value == '\n') {
      scratch.line[lineLen] = '\0';
      if (strncmp(scratch.line, "idxoffsetbits=64", 16) == 0) unsupportedOffsets = true;
      constexpr char SEQUENCE_PREFIX[] = "sametypesequence=";
      if (strncmp(scratch.line, SEQUENCE_PREFIX, sizeof(SEQUENCE_PREFIX) - 1) == 0) {
        const char* sequence = scratch.line + sizeof(SEQUENCE_PREFIX) - 1;
        if (sequence[0] == 'h' && sequence[1] == '\0') {
          format = DefinitionFormat::Html;
        } else if (sequence[0] == 'm' && sequence[1] == '\0') {
          format = DefinitionFormat::Plain;
        } else {
          unsupportedTypes = true;
        }
      }
      lineLen = 0;
      if (value < 0) break;
      continue;
    }
    if (value == '\r') continue;
    if (lineLen + 1 < sizeof(scratch.line)) scratch.line[lineLen++] = static_cast<char>(value);
  }
  file.close();

  if (unsupportedOffsets) {
    LOG_ERR("DICT", "Dictionary uses unsupported 64-bit index offsets: %s", basePath);
    return false;
  }
  if (unsupportedTypes) {
    LOG_ERR("DICT", "Dictionary uses an unsupported sametypesequence: %s", basePath);
    return false;
  }
  return true;
}

bool Dictionary::tryDictionaryFolder(const char* folder, DiscoveryScratch& scratch) {
  HalFile dir = Storage.open(folder);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  scratch.stem[0] = '\0';
  bool ambiguous = false;
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    const size_t nameLen = entry.getName(scratch.name, sizeof(scratch.name));
    const bool isIdx = !entry.isDirectory() && nameLen > 4 && nameLen < sizeof(scratch.name) &&
                       endsWithIgnoreCase(scratch.name, ".idx");
    entry.close();
    if (!isIdx) continue;
    if (scratch.stem[0] != '\0') {
      ambiguous = true;
      break;
    }
    scratch.name[nameLen - 4] = '\0';
    memcpy(scratch.stem, scratch.name, nameLen - 3);
  }
  dir.close();

  if (ambiguous || scratch.stem[0] == '\0') return false;

  const int baseLen = snprintf(scratch.base, sizeof(scratch.base), "%s/%s", folder, scratch.stem);
  if (baseLen <= 0 || static_cast<size_t>(baseLen) >= sizeof(scratch.base)) return false;

  static constexpr const char* REQUIRED_SUFFIXES[] = {".ifo", ".idx", ".dict", ".idx.oft.cspt"};
  for (const char* suffix : REQUIRED_SUFFIXES) {
    if (!composePath(scratch.path, sizeof(scratch.path), scratch.base, suffix) || !Storage.exists(scratch.path)) {
      return false;
    }
  }

  DefinitionFormat format;
  if (!parseIfo(scratch.base, format, scratch)) return false;

  memcpy(basePath_, scratch.base, static_cast<size_t>(baseLen) + 1);
  definitionFormat_ = format;
  LOG_INF("DICT", "Using prepared dictionary %s", basePath_);
  return true;
}

bool Dictionary::discoverInRoot(const char* root, DiscoveryScratch& scratch) {
  if (!Storage.exists(root)) return false;
  if (tryDictionaryFolder(root, scratch)) return true;
  memset(scratch.candidates, 0, sizeof(scratch.candidates));

  HalFile dir = Storage.open(root);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  size_t candidateCount = 0;
  while (candidateCount < MAX_CANDIDATE_FOLDERS) {
    HalFile entry = dir.openNextFile();
    if (!entry) break;
    const size_t nameLen = entry.getName(scratch.name, sizeof(scratch.name));
    const bool candidate = entry.isDirectory() && nameLen > 0 && nameLen < CANDIDATE_NAME_CAPACITY &&
                           scratch.name[0] != '.';
    entry.close();
    if (!candidate) continue;
    memcpy(scratch.candidates + candidateCount * CANDIDATE_NAME_CAPACITY, scratch.name, nameLen + 1);
    candidateCount++;
  }
  dir.close();

  for (size_t i = 0; i < candidateCount; i++) {
    const char* namePtr = scratch.candidates + i * CANDIDATE_NAME_CAPACITY;
    const int count = snprintf(scratch.path, sizeof(scratch.path), "%s/%s", root, namePtr);
    if (count > 0 && static_cast<size_t>(count) < sizeof(scratch.path) &&
        tryDictionaryFolder(scratch.path, scratch)) {
      return true;
    }
  }
  return false;
}

bool Dictionary::discover() {
  basePath_[0] = '\0';
  definitionFormat_ = DefinitionFormat::Typed;
  // Discovery needs several path and directory buffers at the same time.
  // Keep the roughly 2.5 KB workspace off the 4 KB reader task stack, allocate
  // it only after an explicit lookup, then release it before index access.
  auto scratch = makeUniqueNoThrow<DiscoveryScratch>();
  if (!scratch) {
    LOG_ERR("DICT", "OOM: %u-byte dictionary discovery workspace", static_cast<unsigned>(sizeof(DiscoveryScratch)));
    return false;
  }
  for (const auto& root : DICTIONARY_ROOTS) {
    if (discoverInRoot(root, *scratch)) return true;
  }
  LOG_ERR("DICT", "No prepared StarDict dictionary found");
  return false;
}

bool Dictionary::resolveCsptBounds(const char* word, uint32_t& startByte, uint32_t& endByte) const {
  if (!buildPath(pathScratch_, sizeof(pathScratch_), ".idx.oft.cspt")) return false;

  HalFile cspt;
  if (!Storage.openFileForRead("DICT", pathScratch_, cspt)) return false;

  uint8_t header[CSPT_HEADER_SIZE];
  if (cspt.read(header, sizeof(header)) != static_cast<int>(sizeof(header)) ||
      memcmp(header, CSPT_MAGIC, sizeof(CSPT_MAGIC)) != 0 || header[4] != CSPT_VERSION) {
    LOG_ERR("DICT", "Invalid CSPT header: %s", pathScratch_);
    cspt.close();
    return false;
  }

  const uint8_t prefixLen = header[5];
  const uint16_t stride = readLe16(header + 6);
  const uint32_t entryCount = readLe32(header + 8);
  if (prefixLen == 0 || prefixLen > MAX_CSPT_PREFIX_BYTES || stride == 0 || entryCount == 0) {
    LOG_ERR("DICT", "Invalid CSPT dimensions: %s", pathScratch_);
    cspt.close();
    return false;
  }

  const uint32_t entrySize = static_cast<uint32_t>(prefixLen) + 4U;
  const uint64_t requiredSize = CSPT_HEADER_SIZE + static_cast<uint64_t>(entryCount) * entrySize;
  if (requiredSize > cspt.fileSize64()) {
    LOG_ERR("DICT", "Truncated CSPT file: %s", pathScratch_);
    cspt.close();
    return false;
  }

  uint8_t entry[MAX_CSPT_PREFIX_BYTES + 4];
  uint32_t lo = 0;
  uint32_t hi = entryCount;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (!cspt.seekSet(CSPT_HEADER_SIZE + mid * entrySize) ||
        cspt.read(entry, entrySize) != static_cast<int>(entrySize)) {
      cspt.close();
      return false;
    }
    if (comparePrefixToWord(entry, prefixLen, word) <= 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  const uint32_t selected = lo == 0 ? 0 : lo - 1;
  if (!cspt.seekSet(CSPT_HEADER_SIZE + selected * entrySize) ||
      cspt.read(entry, entrySize) != static_cast<int>(entrySize)) {
    cspt.close();
    return false;
  }
  startByte = readLe32(entry + prefixLen);

  if (selected + 1 < entryCount) {
    if (!cspt.seekSet(CSPT_HEADER_SIZE + (selected + 1) * entrySize) ||
        cspt.read(entry, entrySize) != static_cast<int>(entrySize)) {
      cspt.close();
      return false;
    }
    endByte = readLe32(entry + prefixLen);
  } else {
    endByte = UINT32_MAX;
  }
  cspt.close();
  return true;
}

Dictionary::LookupResult Dictionary::locate(const char* word, Hit& hit) const {
  if (!isOpen()) return LookupResult::Unavailable;

  char query[sizeof(hit.headword)];
  if (!cleanWord(word, query, sizeof(query))) return LookupResult::NotFound;

  uint32_t startByte = 0;
  uint32_t endByte = 0;
  if (!resolveCsptBounds(query, startByte, endByte)) return LookupResult::ReadError;

  if (!buildPath(pathScratch_, sizeof(pathScratch_), ".idx")) return LookupResult::ReadError;
  HalFile idx;
  if (!Storage.openFileForRead("DICT", pathScratch_, idx)) return LookupResult::ReadError;

  const uint32_t idxSize = idx.fileSize();
  if (endByte == UINT32_MAX) endByte = idxSize;
  if (startByte >= idxSize || endByte > idxSize || endByte <= startByte || !idx.seekSet(startByte)) {
    LOG_ERR("DICT", "Invalid CSPT range %u..%u for %u-byte index", startByte, endByte, idxSize);
    idx.close();
    return LookupResult::ReadError;
  }

  while (idx.position() < endByte) {
    char headword[sizeof(hit.headword)];
    bool truncatedHeadword = false;
    uint32_t dictOffset = 0;
    uint32_t dictSize = 0;
    if (!readIdxEntry(idx, headword, sizeof(headword), truncatedHeadword, dictOffset, dictSize)) {
      idx.close();
      return LookupResult::ReadError;
    }
    if (truncatedHeadword) continue;

    const int comparison = compareFolded(headword, query);
    if (comparison == 0) {
      hit.offset = dictOffset;
      hit.size = dictSize;
      hit.format = definitionFormat_;
      memcpy(hit.headword, headword, strlen(headword) + 1);
      idx.close();
      return LookupResult::Found;
    }
    if (comparison > 0) break;
  }
  idx.close();
  return LookupResult::NotFound;
}

bool Dictionary::readDefinition(Hit& hit, char* buffer, const size_t capacity, size_t& length,
                                bool& truncated) const {
  length = 0;
  truncated = false;
  if (!isOpen() || !buffer || capacity < 2 || hit.size == 0) return false;

  if (!buildPath(pathScratch_, sizeof(pathScratch_), ".dict")) return false;
  HalFile dict;
  if (!Storage.openFileForRead("DICT", pathScratch_, dict)) return false;

  const uint64_t fileSize = dict.fileSize64();
  const uint64_t end = static_cast<uint64_t>(hit.offset) + hit.size;
  if (end > fileSize || !dict.seekSet(hit.offset)) {
    LOG_ERR("DICT", "Definition slice is outside %s", pathScratch_);
    dict.close();
    return false;
  }

  const size_t available = capacity - 1;
  length = std::min(static_cast<size_t>(hit.size), available);
  truncated = hit.size > available;
  if (dict.read(buffer, length) != static_cast<int>(length)) {
    LOG_ERR("DICT", "Definition read failed at offset %u", hit.offset);
    dict.close();
    length = 0;
    return false;
  }
  dict.close();

  if (hit.format == DefinitionFormat::Typed) {
    if (length < 2 || (buffer[0] != 'm' && buffer[0] != 'h')) {
      LOG_ERR("DICT", "Unsupported typed StarDict definition");
      length = 0;
      return false;
    }
    hit.format = buffer[0] == 'h' ? DefinitionFormat::Html : DefinitionFormat::Plain;
    memmove(buffer, buffer + 1, length - 1);
    length--;
    if (const void* terminator = memchr(buffer, '\0', length)) {
      length = static_cast<const char*>(terminator) - buffer;
      truncated = false;
    }
  }

  // Avoid ending with a partial UTF-8 sequence when the fixed definition
  // budget truncates a large entry.
  if (truncated && length > 0) {
    size_t lead = length - 1;
    while (lead > 0 && (static_cast<uint8_t>(buffer[lead]) & 0xC0) == 0x80) lead--;
    const uint8_t first = static_cast<uint8_t>(buffer[lead]);
    size_t expected = 1;
    if ((first & 0xE0) == 0xC0) expected = 2;
    if ((first & 0xF0) == 0xE0) expected = 3;
    if ((first & 0xF8) == 0xF0) expected = 4;
    if (length - lead < expected) length = lead;
  }
  buffer[length] = '\0';
  return true;
}
