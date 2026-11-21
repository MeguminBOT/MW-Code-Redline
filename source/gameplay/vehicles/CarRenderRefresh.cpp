/** Car render info refreshes, damage, hidden parts, render events and model name hashes (original: zWorld
 * CarRenderInfo::Refresh, SetDamageInfo, RefreshAllRenderInfo, RefreshAllFrontEndCarRenderInfos,
 * Pkt_Car_Service::HidePart, CarRenderConn::OnEvent, VehicleRenderConn::GetRenderMatrix,
 * CarPartModelTable::GetModelNameHash). */
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x739070, CarRenderConnOnEvent)
NFS_REPLACE(0x738b40, VehicleRenderConnGetRenderMatrix)

extern "C" void *__stdcall D3DXVec4Transform(void *out, const void *v, const void *m);

namespace
{
DefragFixer *const kDefragFixer = reinterpret_cast<DefragFixer *>(0x9160b0);
const int kTextureSlots = 3;
const int kTextures = 0x1284;
const int kPartLists = 0x15c;
const int kPartListCount = 0x1c;
const int kPartListNodeTexture = 0x10;
const int kDamage = 0x17a0;
const int kDamageListener = 0x40;
const int kHiddenParts = 0x54;
const int kHiddenPartLimit = 0x4c;
const int kSteerDirection = 0x3d8;
const int kRenderFlags = 0x3f8;

template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

struct Node
{
    Node *next;
    Node *prev;
};

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[offset / 4])(object, args...);
}
} // namespace

void CarRenderInfo::Refresh()
{
    for (int i = 0; i < kTextureSlots; ++i)
    {
        void *&texture = At<void *>(this, kTextures + i * 4);
        texture = kDefragFixer->Fix(texture);
    }
    for (int list = 0; list < kPartListCount; ++list)
    {
        Node *head = &At<Node>(this, kPartLists + list * 8);
        for (Node *n = head->next; n != head; n = n->next)
        {
            void *&texture = At<void *>(n, kPartListNodeTexture);
            if (texture)
                texture = kDefragFixer->Fix(texture);
        }
    }
}

void CarRenderInfo::SetDamageInfo(const DamageZone::Info &info)
{
    unsigned int &current = At<unsigned int>(this, kDamage);
    const unsigned int value = *reinterpret_cast<const unsigned int *>(&info);
    if (value != current)
    {
        current = value;
        if (void *listener = At<void *>(this, kDamageListener))
        {
            if (value)
                Virtual<void, unsigned int *>(listener, 0x14, &current);
            else
                Virtual<void>(listener, 8);
        }
    }
    if (!At<void *>(this, kDamageListener))
        SetPlayerDamage(info);
}

void RefreshAllFrontEndCarRenderInfos(CarType type)
{
    Node *const head = reinterpret_cast<Node *>(0x9b27ec);
    for (Node *n = head->next; n != head; n = n->next)
    {
        if (static_cast<int>(type) != -1 && At<int>(n, 8) != static_cast<int>(type))
            continue;
        if (CarRenderInfo *info = At<CarRenderInfo *>(n, 0x318))
            info->Refresh();
    }
}

void RefreshAllRenderInfo(CarType type)
{
    for (int i = 0; i < *reinterpret_cast<const int *>(0x9b37d4); ++i)
    {
        char *conn = (*reinterpret_cast<char ***>(0x9b37cc))[i];
        if (static_cast<int>(type) != -1 && At<int>(conn, 0x2c) != static_cast<int>(type))
            continue;
        if (At<int>(conn, 0x28) >= 2)
            At<CarRenderInfo *>(conn, 0x44)->Refresh();
    }
    RefreshAllFrontEndCarRenderInfos(type);
}

void RenderConn::Pkt_Car_Service::HidePart(const UCrc32 &part)
{
    const int slot =
        NFS_ORIGINAL(0x73a040, int(__cdecl *)(unsigned int))(*reinterpret_cast<const unsigned int *>(&part));
    if (slot <= 0 || slot >= kHiddenPartLimit)
        return;
    At<unsigned int>(this, kHiddenParts + (slot >> 5) * 4) |= 1u << (slot & 31);
}

void __fastcall CarRenderConnOnEvent(char *conn, void *, int event)
{
    switch (event)
    {
    case 0:
        At<unsigned int>(conn, kRenderFlags) |= 2;
        break;
    case 3:
        At<float>(conn, kSteerDirection) = 1.0f;
        break;
    case 4:
        At<float>(conn, kSteerDirection) = -1.0f;
        break;
    }
}

void __fastcall VehicleRenderConnGetRenderMatrix(char *conn, void *, bMatrix4 *out)
{
    const bMatrix4 *body = At<const bMatrix4 *>(conn, 0x34);
    memcpy(out, body, sizeof(bMatrix4));
    float offset[4];
    D3DXVec4Transform(offset, conn + 0x50, body);
    float *m = reinterpret_cast<float *>(out);
    m[12] -= offset[0];
    m[13] -= offset[1];
    m[14] -= offset[2];
}

unsigned int CarPartModelTable::GetModelNameHash(unsigned int seed, int part, int lod)
{
    const char *self = reinterpret_cast<const char *>(this);
    const unsigned int name = *reinterpret_cast<const unsigned int *>(self + 4 + (part * 5 + lod) * 4);
    if (name == 0xffffffffu)
        return 0;
    if (!self[0])
        return name;
    const unsigned short prefix = *reinterpret_cast<const unsigned short *>(self + 2);
    unsigned int hash = seed;
    if (prefix != 0xffff)
    {
        const char *names =
            *reinterpret_cast<const char *const *>(*reinterpret_cast<const char *const *>(0x9b09d4) + 0xc);
        hash = bStringHash(names + prefix * 4, static_cast<int>(hash));
    }
    hash = bStringHash(reinterpret_cast<const char *>(name), static_cast<int>(hash));
    const char suffix[3] = {'_', static_cast<char>('A' + lod), 0};
    return bStringHash(suffix, static_cast<int>(hash));
}

namespace
{
struct DamagePart
{
    unsigned int mask;
    unsigned int part;
};

const DamagePart kDamageZones[] = {
    {0x7, 0x2e}, {0x38, 0x31}, {0x7000, 0x2f}, {0x38000, 0x30}, {0x1c0000, 0x32}, {0xe00000, 0x33},
};

struct GlassSwap
{
    int slot;
    int shift;
    const char *texture;
};

const GlassSwap kGlassSwaps[] = {
    {0x2dc, 0, "WINDOW_FRONT"},  {0x2e8, 3, "WINDOW_FRONT"},  {0x2f4, 12, "WINDOW_FRONT"},  {0x300, 18, "WINDOW_FRONT"},
    {0x30c, 15, "WINDOW_FRONT"}, {0x318, 21, "WINDOW_FRONT"}, {0x324, 3, "REAR_DEFROSTER"},
};

const unsigned int kBrokenGlass = 0x0a155545;
const int kPlayerDamage = 0x17c8;
} // namespace

void CarRenderInfo::SetPlayerDamage(const DamageZone::Info &info)
{
    const unsigned int damage = *reinterpret_cast<const unsigned int *>(&info);
    unsigned int &current = At<unsigned int>(this, kPlayerDamage);
    if (current == damage)
        return;
    current = damage;
    for (const DamagePart &zone : kDamageZones)
        SetCarDamageState((damage & zone.mask) != 0, zone.part, zone.part);
    for (const GlassSwap &swap : kGlassSwaps)
    {
        const unsigned int level = damage >> swap.shift & 7;
        const unsigned int hash = bStringHash(swap.texture);
        const unsigned int texture = level >= 2 ? kBrokenGlass : hash;
        unsigned int &slot = At<unsigned int>(this, swap.slot);
        if (slot != texture)
        {
            slot = texture;
            At<int>(this, swap.slot + 4) = -1;
        }
    }
}
