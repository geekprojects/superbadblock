//
//

#include <cstdio>
#include <cctype>

#include "utils.h"

uint64_t fletcher64(const uint8_t* data, unsigned int byteCount)
{
    uint64_t sum1 = 0;
    uint64_t sum2 = 0;

    const uint64_t m = 0xFFFFFFFF;
    auto* wordPtr = (uint32_t*) data;

    size_t wordCount = byteCount / sizeof(*wordPtr);
    while (wordCount-- > 0)
    {
        sum1 = (sum1 + *wordPtr++);
        sum1 = (sum1 & m) + (sum1 >> 32);
        sum2 = (sum2 + sum1);
        sum2 = (sum2 & m) + (sum2 >> 32);
    }

    uint64_t check1 = m - ((sum1 + sum2) % m);
    uint64_t check2 = m - ((sum1 + check1) % m);
    return (check2 << 32) | check1;
}

void hexdump(const char* pos, int len)
{
    int i;
    for (i = 0; i < len; i += 16)
    {
        int j;
        printf("%08llx: ", (uint64_t)(pos + i));
        for (j = 0; j < 16 && (i + j) < len; j++)
        {
            printf("%02x ", (uint8_t)pos[i + j]);
        }
        for (j = 0; j < 16 && (i + j) < len; j++)
        {
            char c = pos[i + j];
            if (!isprint(c))
            {
                c = '.';
            }
            printf("%c", c);
        }
        printf("\n");
    }
}
