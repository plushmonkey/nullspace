#include "Platform.h"

#ifdef _WIN32
#ifdef APIENTRY
// Fix warning with glad definition
#undef APIENTRY
#endif
#endif

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace null {

#ifdef _WIN32

bool CreateFolder(const char* path) { return CreateDirectory(path, NULL); }

inline bool IsValidCharacter(char c) {
  // TODO: fontf
  return c >= ' ' && c <= '~';
}

void PasteClipboard(char* dest, size_t available_size) {
  if (OpenClipboard(NULL)) {
    if (IsClipboardFormatAvailable(CF_TEXT)) {
      HANDLE handle = GetClipboardData(CF_TEXT);
      char* data = (char*)GlobalLock(handle);

      if (data) {
        for (size_t i = 0; i < available_size && data[i]; ++i, ++dest) {
          if (null::IsValidCharacter(data[i])) {
            *dest = data[i];
          }
        }
        *dest = 0;

        GlobalUnlock(data);
      }
    }

    CloseClipboard();
  }
}

#else
bool CreateFolder(const char* path) { return false; }
#endif

}  // namespace null