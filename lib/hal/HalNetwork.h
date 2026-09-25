#pragma once

class HalNetwork final {
 public:
  bool enableTcpDontFragmentOnStation();
};

extern HalNetwork halNetwork;
