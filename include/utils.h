#ifndef SUPERBADBLOCK_UTILS_H
#define SUPERBADBLOCK_UTILS_H

#include <stdint.h>
#include <cstdlib>

#define ALIGN(V, SIZE) ((((V) + (SIZE) - 1) / (SIZE)) * (SIZE))

uint64_t fletcher64(const uint8_t* data, unsigned int byteCount);
void hexdump(const char* pos, int len);

#endif //SUPERBADBLOCK_UTILS_H
