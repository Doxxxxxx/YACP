#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

constexpr int HIGH = 1;
constexpr int LOW = 0;
constexpr int INPUT = 1;
constexpr int OUTPUT = 3;
constexpr int INPUT_PULLUP = 5;

void pinMode(int, int);
void digitalWrite(int, int);
int digitalRead(int);
void delay(unsigned long);
void delayMicroseconds(unsigned int);
unsigned long millis();

struct SerialStub {
  explicit operator bool() const { return false; }
  template <typename... Args>
  void printf(const char*, Args...) {}
};

inline SerialStub Serial;
