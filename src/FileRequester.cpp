#include "FileRequester.h"

#include <memory.h>
#include <stdio.h>

#include "Inflate.h"
#include "Memory.h"
#include "Platform.h"
#include "net/Checksum.h"
#include "net/Connection.h"
#include "net/PacketDispatcher.h"

namespace null {

extern const char* kServerName;
static const char* zone_folder = "zones";

static void GetFilePath(char* buffer, const char* filename) {
  sprintf(buffer, "%s/%s/%s", zone_folder, kServerName, filename);
}

static void CreateZoneFolder() {
  char path[260];

  CreateFolder(zone_folder);
  sprintf(path, "%s/%s", zone_folder, kServerName);
  CreateFolder(path);
}

inline bool FileExists(MemoryArena& temp_arena, const char* filename, u32 checksum) {
  char path[260];
  GetFilePath(path, filename);

  FILE* file = fopen(path, "rb");
  if (!file) return false;

  fseek(file, 0, SEEK_END);
  size_t file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  u8* file_data = temp_arena.Allocate(file_size);

  fread(file_data, 1, file_size, file);

  fclose(file);

  u32 crc = crc32(file_data, file_size);

  fclose(file);

  return crc == checksum;
}

static void OnCompressedMapPkt(void* user, u8* pkt, size_t size) {
  FileRequester* requester = (FileRequester*)user;

  requester->OnCompressedFile(pkt, size);
}

FileRequester::FileRequester(MemoryArena& perm_arena, MemoryArena& temp_arena, Connection& connection,
                             PacketDispatcher& dispatcher)
    : perm_arena(perm_arena), temp_arena(temp_arena), connection(connection) {
  dispatcher.Register(ProtocolS2C::CompressedMap, OnCompressedMapPkt, this);
}

void FileRequester::OnCompressedFile(u8* pkt, size_t size) {
  if (current == nullptr) return;

  u8* data = pkt + 17;
  mz_ulong data_size = (mz_ulong)size - 17;

  ArenaSnapshot snapshot = temp_arena.GetSnapshot();

  if (current->decompress) {
    mz_ulong compressed_size = data_size;
    u8* uncompressed;
    int status;

    do {
      data_size *= 2;

      // Reset arena and try to allocate new space for the increased buffer
      temp_arena.Revert(snapshot);
      uncompressed = temp_arena.Allocate(data_size);

      status = mz_uncompress(uncompressed, &data_size, data, compressed_size);
    } while (status == MZ_BUF_ERROR);

    if (status != MZ_OK) {
      fprintf(stderr, "Failed to uncompress map data.\n");
    } else {
      data = uncompressed;
    }
  }

  CreateZoneFolder();

  FILE* f = fopen(current->filename, "wb");
  if (f) {
    fwrite(data, 1, data_size, f);
    fclose(f);
  } else {
    fprintf(stderr, "Failed to open %s for writing.\n", current->filename);
  }

  current->callback(current->user, current, data);

  temp_arena.Revert(snapshot);

  if (current == requests) {
    requests = requests->next;
  } else {
    FileRequest* check = requests;
    while (check) {
      if (check->next == current) {
        check->next = current->next;
        break;
      }
      check = check->next;
    }
  }

  current->next = free;
  free = current;
  current = requests;

  if (current) {
    SendNextRequest();
  }
}

void FileRequester::Request(const char* filename, u16 index, u32 size, u32 checksum, bool decompress,
                            RequestCallback callback, void* user) {
  FileRequest* request = free;

  if (request) {
    free = free->next;
  } else {
    request = memory_arena_push_type(&perm_arena, FileRequest);
  }

  GetFilePath(request->filename, filename);
  request->index = index;
  request->size = size;
  request->checksum = checksum;
  request->callback = callback;
  request->user = user;
  request->decompress = decompress;

  if (FileExists(temp_arena, filename, checksum)) {
    FILE* f = fopen(request->filename, "rb");
    fseek(f, 0, SEEK_END);
    long filesize = ftell(f);
    fseek(f, 0, SEEK_SET);

    ArenaSnapshot snapshot = temp_arena.GetSnapshot();
    u8* data = (u8*)temp_arena.Allocate(filesize);

    fread(data, 1, filesize, f);
    fclose(f);

    callback(user, request, data);

    temp_arena.Revert(snapshot);

    request->next = free;
    free = request;
    return;
  }

  request->next = requests;
  requests = request;

  if (current == nullptr) {
    SendNextRequest();
  }
}

void FileRequester::SendNextRequest() {
  current = requests;

#pragma pack(push, 1)
  struct {
    u8 type;
    u16 index;
  } file_request = {0x0C, current->index};
#pragma pack(pop)

  connection.packet_sequencer.SendReliableMessage(connection, (u8*)&file_request, sizeof(file_request));
}

}  // namespace null
