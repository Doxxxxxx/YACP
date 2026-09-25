#pragma once
#include <cstddef>
#include <cstdint>
struct TestEsp {
  unsigned getFreeHeap() const { return 100000; }
  unsigned getMaxAllocHeap() const { return 50000; }
};
inline TestEsp ESP;
inline void delay(int) {}
