/**
 * Small helpers of the car renderer (original: zWorld CarRenderInfo, VehicleRenderConn, TireState::Effect).
 *
 * CarRenderInfo: +0x23c the replacement texture table (0xdb dwords), +0x5a8 the copy used for damage, +0xb84 the part
 * models (5 LODs of 0x14 bytes per part id, up to 0x4d parts; low two bits of an entry: 1 hidden), +0x15c8 a texture
 * name hash, +0x1628/+0x162c the LOD range in use, +0x17c0 flashing on, +0x17c4 its timer.
 */
#include <math.h>
#include <new>
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "gameplay/world/weather.h"

NFS_REPLACE(0x738c60, NotifyTireStateEffectOfEmitterDelete)
NFS_REPLACE(0x738920, HandleEmitterGroupDelete)
NFS_REPLACE(0x744c70, VehicleCheckForRain)
NFS_REPLACE(0x7445c0, CarGetEmitterPositions)

void __cdecl NotifyTireStateEffectOfEmitterDelete(void *effect, EmitterGroup *);
void __cdecl HandleEmitterGroupDelete(void *owner, EmitterGroup *);
bool __fastcall VehicleCheckForRain(VehicleRenderConn *self, void *, eView *view);
int __fastcall CarGetEmitterPositions(CarRenderInfo *self, void *, void *listObject, const unsigned int *names, int nameCount);

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

const int kPartCount = 0x4d;
const unsigned int kPartPointerMask = 0xfffffffcu;

unsigned int *PartSlot(void *info, int lod, int part)
{
    return &At<unsigned int>(info, 0xb84 + lod * 4 + part * 0x14);
}
} // namespace

void CarRenderInfo::UpdateFlashing()
{
    if (!At<char>(this, 0x17c0))
        return;
    const float timer = RealTimeElapsed + At<float>(this, 0x17c4);
    At<float>(this, 0x17c4) = timer;
    if (timer > 0.05f)
        At<float>(this, 0x17c4) = timer - 0.120000005f;
}

void CarRenderInfo::SetCarDamageState(bool intact, unsigned int firstPart, unsigned int lastPart)
{
    const unsigned int hiddenBit = intact ? 0 : 1;
    for (int lod = At<int>(this, 0x1628); lod <= At<int>(this, 0x162c); ++lod)
    {
        if (firstPart > lastPart)
            continue;
        for (unsigned int part = firstPart; part <= lastPart; ++part)
        {
            unsigned int *slot = PartSlot(this, lod, static_cast<int>(part));
            const unsigned int model = *slot & kPartPointerMask;
            if (model)
                *slot = model | hiddenBit;
        }
    }
}

unsigned int CarRenderInfo::HideCarPart(int part, bool hide)
{
    unsigned int result = 0;
    if (part >= kPartCount)
        return 0;
    const int firstLod = At<int>(this, 0x1628);
    for (int lod = firstLod; lod <= At<int>(this, 0x162c); ++lod)
    {
        unsigned int *slot = PartSlot(this, lod, part);
        const unsigned int model = *slot & kPartPointerMask;
        *slot = model | (hide ? 1 : 0);
        if (lod == firstLod && model)
            result = *reinterpret_cast<unsigned int *>(model + 8);
    }
    return result;
}

void CarRenderInfo::UpdateCarReplacementTextures()
{
    unsigned int *from = &At<unsigned int>(this, 0x23c);
    unsigned int *to = &At<unsigned int>(this, 0x5a8);
    for (int i = 0; i < 0xdb; ++i)
        to[i] = from[i];
    const unsigned int carbon = bStringHash("CARBONFIBRE");
    const int carbonSlots[3] = {0x5ac, 0x5d0, 0x5dc};
    const int ownSlots[2] = {0x5b8, 0x5c4};
    if (carbon != At<unsigned int>(this, carbonSlots[0]))
    {
        At<unsigned int>(this, carbonSlots[0]) = carbon;
        At<unsigned int>(this, carbonSlots[0] + 4) = 0xffffffff;
    }
    const unsigned int skin = At<unsigned int>(this, 0x15c8);
    for (int slot : ownSlots)
        if (skin != At<unsigned int>(this, slot))
        {
            At<unsigned int>(this, slot) = skin;
            At<unsigned int>(this, slot + 4) = 0xffffffff;
        }
    for (int i = 1; i < 3; ++i)
        if (carbon != At<unsigned int>(this, carbonSlots[i]))
        {
            At<unsigned int>(this, carbonSlots[i]) = carbon;
            At<unsigned int>(this, carbonSlots[i] + 4) = 0xffffffff;
        }
}

void CarRenderInfo::RenderPart(eView *view, CarPartModel *model, bMatrix4 *matrix, eDynamicLightContext *lights,
                               unsigned int flags)
{
    if (!model)
        return;
    const unsigned int pointer = *reinterpret_cast<unsigned int *>(model) & kPartPointerMask;
    eModel *drawn = pointer ? reinterpret_cast<eModel *>(pointer) : reinterpret_cast<eModel *>(&StandardDebugModel);
    reinterpret_cast<eViewPlatInterface *>(view)->Render(drawn, matrix, reinterpret_cast<eLightContext *>(lights), flags, nullptr);
}

unsigned int VehicleRenderConn::FindPart(CAR_PART_ID part)
{
    const int id = static_cast<int>(part);
    char *info = At<char *>(this, 0x44);
    if (id < 0 || !info || id >= kPartCount)
        return 0;
    const unsigned int model = *PartSlot(info, At<int>(info, 0x1628), id) & kPartPointerMask;
    return model ? *reinterpret_cast<unsigned int *>(model + 8) : 0;
}

void __cdecl HandleEmitterGroupDelete(void *owner, EmitterGroup *)
{
    At<void *>(owner, 0x50) = nullptr;
    At<int>(owner, 0x54) = 0;
}

void __cdecl NotifyTireStateEffectOfEmitterDelete(void *effectObject, EmitterGroup *)
{
    char *effect = static_cast<char *>(effectObject);
    for (int offset = 4; offset <= 0x14; offset += 4)
        At<int>(effect, offset) = 0;
    effect[0] = 1;
}

void __cdecl CloseCarEffects()
{
    bDeleteSlotPool(CarEmitterPositionSlotPool);
}

void TireState::Effect::FreeUpFX()
{
    char *self = reinterpret_cast<char *>(this);
    if (EmitterGroup *group = At<EmitterGroup *>(self, 8))
    {
        At<unsigned int>(group, 0x18) |= 0x80000;
        group->UnSubscribe();
    }
    self[0] = 1;
    At<void *>(self, 8) = nullptr;
    At<int>(self, 0x14) = 0;
}

void CarPartCuller::InitPart(eCullableCarParts part, const bVector3 *centre)
{
    const int index = static_cast<int>(part);
    char *table = reinterpret_cast<char *>(0x905040) + index * 0x50;
    const int normals = At<int>(table, 0xc);
    for (int i = 0; i < normals; ++i)
    {
        bVector3 *n = reinterpret_cast<bVector3 *>(table + 0x10 + i * 0x10);
        if (fabsf(1.0f - sqrtf(n->x * n->x + n->y * n->y + n->z * n->z)) > 0.01f)
            bNormalize(n, n);
    }
    float *out = &At<float>(this, index * 0x20);
    out[0] = centre->x;
    out[1] = centre->y;
    out[2] = centre->z;
}

CarEmitterPosition::CarEmitterPosition(float x, float y, float z)
{
    At<float>(this, 4) = x;
    At<void *>(this, 0x10) = nullptr;
    At<float>(this, 8) = y;
    At<float>(this, 0xc) = z;
}

/** CarRenderInfo::GetEmitterPositions (thiscall): every position marker on the car named in `names`, appended to `listObject`. */
int __fastcall CarGetEmitterPositions(CarRenderInfo *self, void *, void *listObject, const unsigned int *names, int nameCount)
{
    if (!At<void *>(self, 0x88))
        return 0;
    char *list = static_cast<char *>(listObject);
    int count = 0;
    for (int part = 0; part < 0x17c; part += 5)
    {
        const unsigned int slot = At<unsigned int>(self, 0xb84 + (At<int>(self, 0x1628) + part) * 4);
        eModel *model = reinterpret_cast<eModel *>(slot & kPartPointerMask);
        if (!model)
            continue;
        for (ePositionMarker *marker = model->GetPostionMarker(nullptr); marker; marker = model->GetPostionMarker(marker))
            for (int i = 0; i < nameCount; ++i)
            {
                if (At<unsigned int>(marker, 0) != names[i])
                    continue;
                ++count;
                char *position = static_cast<char *>(bOMalloc(CarEmitterPositionSlotPool));
                if (!position)
                    continue;
                At<ePositionMarker *>(position, 0x10) = marker;
                At<float>(position, 4) = At<float>(marker, 0x40);
                At<float>(position, 8) = At<float>(marker, 0x44);
                At<float>(position, 0xc) = At<float>(marker, 0x48);
                char *tail = At<char *>(list, 4);
                At<char *>(list, 4) = position;
                At<char *>(tail, 0) = position;
                At<char *>(position, 0) = list;
            }
    }
    return count;
}

namespace
{
/** A camera anchor list (head at `link`): whether its first anchor follows the vehicle with this id. */
bool ListAnchorIs(unsigned int linkAddress, int vehicleId)
{
    char *first = *reinterpret_cast<char **>(linkAddress);
    if (first == reinterpret_cast<char *>(linkAddress) || !first)
        return false;
    char *anchor = first - 4;
    char *target = (*reinterpret_cast<char *(__thiscall ***)(void *)>(anchor))[4](anchor);
    return target && At<int>(target, 0x94) == vehicleId;
}
} // namespace

bool VehicleRenderConn::IsViewAnchor() const
{
    const int id = At<int>(const_cast<VehicleRenderConn *>(this), 0x30);
    return ListAnchorIs(0x919694, id) || ListAnchorIs(0x919704, id);
}

/** VehicleRenderConn::CheckForRain (thiscall): rain on the car, by distance from the view of this car and its anchor. */
/** The tyres run wet (their surface's wet effects: spray, wet skids) where the roads are this wet, as the weather soaks
 * and dries them (the original asked whether the view's rain was falling, so its spray started with the first drop on
 * a dry road and stopped with the last on a soaked one). */
const float kWetTyres = 0.3f;

bool __fastcall VehicleCheckForRain(VehicleRenderConn *self, void *, eView *view)
{
    if (!view)
        return false;
    char *weather = At<char *>(view, 0x68);
    if (!weather || !(nfs::weather::Wetness() > kWetTyres))
        return false;
    void *mover = NFS_ORIGINAL(0x410d40, void *(__thiscall *)(eView *))(view);
    if (!mover)
        return false;
    const float *position = &At<float>(At<char *>(self, 0x34), 0x30);
    const float distance = NFS_ORIGINAL(0x4aab60, float(__thiscall *)(void *, const float *))(mover, position);
    if (!(distance < 60.0f))
        return false;
    const bool nearOrOwn = distance < 10.0f ||
                           NFS_ORIGINAL(0x468bc0, int(__thiscall *)(void *))(mover) == At<int>(self, 0x30);
    return !At<int>(weather, nearOrOwn ? 0x234 : 0x238);
}

VehicleRenderConn *__cdecl VehicleRenderConn::Find(unsigned int id)
{
    VehicleRenderConn **conn = *reinterpret_cast<VehicleRenderConn ***>(0x9b37cc);
    VehicleRenderConn **end = conn + *reinterpret_cast<int *>(0x9b37d4);
    for (; conn != end; ++conn)
        if (At<unsigned int>(*conn, 0x30) == id)
            return *conn;
    return nullptr;
}

void __cdecl VehicleRenderConn::FetchData(float dt)
{
    VehicleRenderConn **conn = *reinterpret_cast<VehicleRenderConn ***>(0x9b37cc);
    VehicleRenderConn **end = conn + *reinterpret_cast<int *>(0x9b37d4);
    for (; conn != end; ++conn)
        (*reinterpret_cast<void(__thiscall ***)(void *, float)>(*conn))[4](*conn, dt);
}

void TireState::Effect::LazyInit()
{
    char *self = reinterpret_cast<char *>(this);
    if (EmitterGroup *group = At<EmitterGroup *>(self, 8))
    {
        At<unsigned int>(group, 0x18) |= 0x80000;
        group->UnSubscribe();
    }
    At<void *>(self, 8) = nullptr;
    const unsigned int key = At<unsigned int>(self, 4);
    if (!key || key == 0xeec2271a)
        return;
    alignas(4) unsigned char storage[0x14];
    new (storage) Attrib::Gen::emittergroup(key, 0, nullptr);
    if (const Attrib::Collection *collection = At<const Attrib::Collection *>(storage, 4))
    {
        EmitterGroup *group = gEmitterSystem.CreateEmitterGroup(collection, 0x40000000);
        At<EmitterGroup *>(self, 8) = group;
        if (group)
        {
            group->Enable();
            group->SubscribeToDeletion(self, &NotifyTireStateEffectOfEmitterDelete);
        }
        self[0] = 0;
        At<int>(self, 0x14) = 0;
    }
    reinterpret_cast<Attrib::Instance *>(storage)->~Instance();
}

void TireState::Effect::Update(float value, const bVector3 *velocity, const bMatrix4 *orientation, float dt, const bVector4 &position)
{
    char *self = reinterpret_cast<char *>(this);
    const float intensity = UMath::Ramp(value, At<float>(self, 0xc), At<float>(self, 0x10));
    if (!(intensity <= 0.0f))
    {
        if (self[0])
            LazyInit();
        EmitterGroup *group = At<EmitterGroup *>(self, 8);
        if (!group || !(intensity > 0.0f))
            return;
        float world[16];
        memcpy(world, orientation, sizeof(world));
        world[12] = position.x;
        world[13] = position.y;
        world[14] = position.z;
        world[15] = 1.0f;
        group->SetLocalWorld(reinterpret_cast<const bMatrix4 *>(world));
        group->SetInheritVelocity(velocity);
        At<float>(group, 0x68) = intensity;
        group->Update(dt);
        if (!group->GetNumParticles())
            ++At<int>(self, 0x14);
        if (At<int>(self, 0x14) <= 10)
            return;
    }
    EmitterGroup *group = At<EmitterGroup *>(self, 8);
    if (!group)
        return;
    At<unsigned int>(group, 0x18) |= 0x80000;
    group->UnSubscribe();
    At<void *>(self, 8) = nullptr;
    self[0] = 1;
    At<int>(self, 0x14) = 0;
}

namespace
{
/** A light flare made from a part model's position marker: the marker's name and the flare's type (and size class:
 * 2 for the small lamps, 4 for the rest). The two brake-light keys go to one part only, the first that has them. */
struct FlareKind
{
    unsigned int marker;
    int type;
};
const FlareKind kFlareKinds[] = {{0x41489594u, 10}, {0x1e4150b4u, 5},  {0x28cd78f5u, 12}, {0x31a66786u, 1},
                                 {0x6a52a241u, 11}, {0x7a5bcf69u, 0},  {0xbf700a79u, 1},  {0x9db90133u, 0},
                                 {0xa2a2fc7cu, 1},  {0xb4348dbau, 7},  {0xd09091c6u, 0},  {0xe662c161u, 6}};
const unsigned int kFlareOncePerPartA = 0x7a5b2f25u;
const unsigned int kFlareOncePerPartB = 0x7adf7ef8u;
const int kFlareOncePerPartType = 3;
const int kLightPartCount = 0x4c;
const int kFlareList = 0x104;
const int kHasFlares = 0x88;
const int kInfoFirstLod = 0x1628;
void *const kFastMem = reinterpret_cast<void *>(0x925b30);

bool SmallFlare(int type)
{
    return type == 5 || type == 6 || type == 7 || type == 10 || type == 11 || type == 12;
}
} // namespace

void CarRenderInfo::CreateCarLightFlares()
{
    if (!At<int>(this, kHasFlares))
        return;
    int partA = -1;
    int partB = -1;
    for (int part = kLightPartCount - 1; part >= 0; --part)
    {
        eModel *model = reinterpret_cast<eModel *>(*PartSlot(this, At<int>(this, kInfoFirstLod), part) & kPartPointerMask);
        if (!model)
            continue;
        for (ePositionMarker *marker = model->GetPostionMarker(nullptr); marker; marker = model->GetPostionMarker(marker))
        {
            const unsigned int key = *reinterpret_cast<unsigned int *>(marker);
            int type = -1;
            if (key == kFlareOncePerPartA || key == kFlareOncePerPartB)
            {
                int &owner = key == kFlareOncePerPartA ? partA : partB;
                if (owner != part && owner > 0)
                    continue;
                owner = part;
                type = kFlareOncePerPartType;
            }
            else
                for (const FlareKind &kind : kFlareKinds)
                    if (kind.marker == key)
                        type = kind.type;
            if (type < 0)
                continue;
            char *flare = static_cast<char *>(
                NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(kFastMem, 0x30, nullptr));
            const char *m = reinterpret_cast<const char *>(marker);
            At<unsigned int>(flare, 8) = key;
            At<char>(flare, 0x2c) = static_cast<char>(type);
            At<char>(flare, 0x2d) = SmallFlare(type) ? 2 : 4;
            for (int i = 0; i < 3; ++i)
            {
                At<unsigned int>(flare, 0x10 + i * 4) = *reinterpret_cast<const unsigned int *>(m + 0x40 + i * 4);
                At<unsigned int>(flare, 0x20 + i * 4) = *reinterpret_cast<const unsigned int *>(m + 0x30 + i * 4);
            }
            At<unsigned int>(flare, 0x1c) = 0;
            char *head = reinterpret_cast<char *>(this) + kFlareList;
            char *tail = At<char *>(head, 4);
            At<char *>(tail, 0) = flare;
            At<char *>(head, 4) = flare;
            At<char *>(flare, 4) = tail;
            At<char *>(flare, 0) = head;
        }
    }
}
