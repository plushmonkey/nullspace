#ifndef NULLSPACE_BUFFER_H_
#define NULLSPACE_BUFFER_H_

#include "Memory.h"
#include "Types.h"

namespace null {

struct NetworkBuffer {
  NetworkBuffer() : data(nullptr), read(nullptr), write(nullptr), size(0) {}
  NetworkBuffer(u8* data, size_t size, size_t write_offset = 0) : data(data), read(data), write(data + write_offset), size(size) {}
  NetworkBuffer(MemoryArena& arena, size_t size);

  void WriteU8(u8 value);
  void WriteU16(u16 value);
  void WriteU32(u32 value);
  void WriteFloat(float value);
  void WriteString(const char* str, size_t size);

  u8 ReadU8();
  u16 ReadU16();
  u32 ReadU32();
  float ReadFloat();
  char* ReadString(size_t size);

  inline size_t GetSize() const { return (size_t)(write - data); }
  inline void Reset() { this->write = this->read = this->data; }

  u8* data;
  u8* read;
  u8* write;
  size_t size;
};

}  // namespace null

#endif
