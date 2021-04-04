#include "MapHandler.h"

#include <cstdio>

#include "Buffer.h"
#include "Checksum.h"
#include "Inflate.h"
#include "net/Connection.h"

// TODO: Is filename always null terminated?

namespace null {

bool MapHandler::OnMapInformation(Connection& connection, u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();  // 0x29
  char* filename = buffer.ReadString(16);
  this->checksum = buffer.ReadU32();
  // TODO: Store this somewhere so it can be compared while downloading huge chunks for progress percent
  u32 compressed_size = buffer.ReadU32();

  FILE* file = fopen(filename, "rb");

  if (file) {
    // Read the file and check crc against server-provided checksum.
    // If it's not a match then remove the local file.
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    u8* file_data = temp_arena.Allocate(file_size);

    fread(file_data, 1, file_size, file);

    fclose(file);

    u32 crc = crc32(file_data, file_size);

    if (crc != checksum) {
      remove(filename);
    }
  }

  // Try to load the file. If the crc didn't match above then it will request the map data
  if (!map.Load(perm_arena, filename)) {
    u8 request = 0x0c;
    connection.Send(&request, 1);
    return false;
  }

  return true;
}

bool MapHandler::OnCompressedMap(Connection& connection, u8* pkt, size_t size) {
  ArenaSnapshot snapshot = temp_arena.GetSnapshot();

  mz_ulong uncompressed_size = (mz_ulong)size;
  mz_ulong compressed_size = (mz_ulong)(size - 1 - 16);
  u8* uncompressed;
  int status;

  char* filename = (char*)(pkt + 1);
  // Offset after type and filename
  u8* compressed = pkt + 1 + 16;

  do {
    uncompressed_size *= 2;

    // Reset arena and try to allocate new space for the increased buffer
    temp_arena.Revert(snapshot);
    uncompressed = temp_arena.Allocate(uncompressed_size);

    status = mz_uncompress(uncompressed, &uncompressed_size, compressed, compressed_size);
  } while (status == MZ_BUF_ERROR);

  if (status != MZ_OK) {
    fprintf(stderr, "Failed to uncompress map data.\n");
    return false;
  }

  FILE* file = fopen(filename, "wb");

  if (!file) {
    fprintf(stderr, "Failed to open map %s for writing.\n", filename);
    return false;
  }

  fwrite(uncompressed, 1, uncompressed_size, file);
  fclose(file);

  temp_arena.Revert(snapshot);

  return map.Load(perm_arena, filename);
}

}  // namespace null
