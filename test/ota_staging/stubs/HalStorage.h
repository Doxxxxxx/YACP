#pragma once
#include <cstddef>
class HalFile {
 public:
  bool opened = false;
  size_t offset = 0;
  explicit operator bool() const { return opened; }
  size_t fileSize();
  int read(void*, size_t);
  void close();
};
struct TestStorage {
  bool ensureDirectoryExists(const char*);
  bool exists(const char*);
  bool remove(const char*);
  bool openFileForRead(const char*, const char*, HalFile&);
};
inline TestStorage Storage;
