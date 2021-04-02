#include "Buffer.h"

#include <cassert>
#include <cstdlib>
#include <cstring>

// TODO: Protocol is little endian. Should convert on big endian
#ifdef _MSC_VER
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#else
#define bswap_16(x) __builtin_bswap16(x)
#define bswap_32(x) __builtin_bswap32(x)
#define bswap_64(x) __builtin_bswap64(x)
#endif

namespace null {

NetworkBuffer::NetworkBuffer(MemoryArena& arena, size_t size) : data(nullptr), read(nullptr), write(nullptr), size(0) {
  this->data = arena.Allocate(size);

  assert(this->data);

  this->read = this->write = this->data;
  this->size = size;
}

void NetworkBuffer::WriteU8(u8 value) {
  assert(this->write + sizeof(value) <= this->data + this->size);

  *this->write = value;
  this->write += sizeof(value);
}

void NetworkBuffer::WriteU16(u16 value) {
  assert(this->write + sizeof(value) <= this->data + this->size);

  memcpy(this->write, &value, sizeof(value));
  this->write += sizeof(value);
}

void NetworkBuffer::WriteU32(u32 value) {
  assert(this->write + sizeof(value) <= this->data + this->size);

  memcpy(this->write, &value, sizeof(value));
  this->write += sizeof(value);
}

void NetworkBuffer::WriteFloat(float value) {
  assert(this->write + sizeof(value) <= this->data + this->size);

  memcpy(this->write, &value, sizeof(value));
  this->write += sizeof(value);
}

void NetworkBuffer::WriteString(const char* str, size_t size) {
  assert(this->write + size <= this->data + this->size);

  memcpy(this->write, str, size);
  this->write += size;
}

u8 NetworkBuffer::ReadU8() {
  assert(this->read + sizeof(u8) <= this->data + this->size);

  u8 result = *this->read;

  this->read += sizeof(u8);

  return result;
}

u16 NetworkBuffer::ReadU16() {
  assert(this->read + sizeof(u16) <= this->data + this->size);

  u16 result;

  memcpy(&result, this->read, sizeof(result));
  this->read += sizeof(result);

  return result;
}

u32 NetworkBuffer::ReadU32() {
  assert(this->read + sizeof(u32) <= this->data + this->size);

  u32 result;

  memcpy(&result, this->read, sizeof(result));
  this->read += sizeof(result);

  return result;
}

float NetworkBuffer::ReadFloat() {
  assert(this->read + sizeof(float) <= this->data + this->size);

  float result;

  memcpy(&result, this->read, sizeof(result));
  this->read += sizeof(result);

  return result;
}

char* NetworkBuffer::ReadString(size_t size) {
  assert(this->read + size <= this->data + this->size);

  char* result = (char*)this->read;
  this->read += size;

  return result;
}

}  // namespace null
