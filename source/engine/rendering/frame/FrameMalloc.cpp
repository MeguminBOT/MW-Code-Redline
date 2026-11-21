/** Per-frame matrix allocation (original: zAI eFrameMallocMatrix). */
#include "game/gen/api.h"

namespace
{
const unsigned int kAlign = 16;
}

bMatrix4 *eFrameMallocMatrix(int count)
{
    const unsigned int bytes = (static_cast<unsigned int>(count) * sizeof(bMatrix4) + kAlign - 1) & ~(kAlign - 1);
    unsigned char *at = CurrentBufferPos;
    if (at + bytes >= CurrentBufferEnd)
    {
        FrameMallocFailed = 1;
        FrameMallocFailAmount += bytes;
        return nullptr;
    }
    CurrentBufferPos = at + bytes;
    return reinterpret_cast<bMatrix4 *>(at);
}
