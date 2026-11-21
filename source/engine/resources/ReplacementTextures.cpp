/** A model's texture replacement entry (original: zAnim eReplacementTextureTable constructor). */
#include "game/gen/api.h"

eReplacementTextureTable::eReplacementTextureTable()
{
    unsigned int *self = reinterpret_cast<unsigned int *>(this);
    self[0] = 0;
    self[1] = 0;
    self[2] = 0xffffffffu;
}
