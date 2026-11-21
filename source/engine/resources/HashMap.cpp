/**
 * The attribute database's hash map (original: Attrib::HashMap): open addressing over twelve-byte nodes, each with how far past its home slot
 * the longest search from there goes. A node whose pointer points at itself and that is not flagged as in use is free, and counts as key 0.
 */
#include "game/gen/api.h"

namespace
{
struct Node
{
    unsigned int key;
    const void *pointer;
    unsigned short unknown;
    unsigned char search;
    unsigned char flags;
};
static_assert(sizeof(Node) == 12, "Attrib::Node");

struct Map
{
    const Node *table;
    unsigned int size;
    unsigned int entries;
    unsigned short unknown;
    unsigned short shift;
};

const unsigned char kNodeInUse = 0x10;

inline unsigned int KeyOf(const Node &node)
{
    return !(node.flags & kNodeInUse) && node.pointer == &node ? 0 : node.key;
}

inline unsigned int Rotated(unsigned int key, unsigned int shift)
{
    shift &= 0x1f;
    return shift ? key << shift | key >> (32 - shift) : key;
}
} // namespace

unsigned int Attrib::HashMap::FindIndex(unsigned int key) const
{
    const Map &map = *reinterpret_cast<const Map *>(this);
    if (!map.entries || !key)
        return map.size;
    const unsigned int size = map.size;
    unsigned int slot = Rotated(key, static_cast<unsigned char>(map.shift)) % size;
    const unsigned int search = map.table[slot].search;
    for (unsigned int i = 0; i < search; ++i)
    {
        if (KeyOf(map.table[slot]) == key)
            break;
        slot = (slot + 1) % size;
    }
    return KeyOf(map.table[slot]) == key ? slot : size;
}
