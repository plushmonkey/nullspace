#ifndef NULLSPACE_PLATFORM_H_
#define NULLSPACE_PLATFORM_H_

#include <cstdlib>

namespace null {

bool CreateFolder(const char* path);
void PasteClipboard(char* dest, size_t available_size);

int null_stricmp(const char* s1, const char* s2);

}  // namespace null

#endif
