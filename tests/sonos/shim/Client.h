// The slice of Arduino's Client the XML scanner reads through.
#pragma once
#include <cstddef>
#include <cstdint>

class Client {
 public:
  virtual ~Client() {}
  virtual int available() = 0;
  virtual int read(uint8_t* buf, size_t n) = 0;
  virtual uint8_t connected() = 0;
};
