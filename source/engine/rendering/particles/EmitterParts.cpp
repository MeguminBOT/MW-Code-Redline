/**
 * Parts of the particle emitters (original: zEcstasy): a new particle's size, turn and colour from the emitter's curves,
 * its velocity in a disc or cone, animated texture cells, the libraries' world triggers, and the emitters' on/off
 * timing (EmitterControl).
 *
 * Angles are in degrees in the attributes and become the engine's 0x10000-a-turn units by (int)(degrees * 65536) / 360.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x4ff5f0, FirstWord)
NFS_REPLACE(0x4ffc00, FirstWord)
NFS_REPLACE(0x500000, UnlinkListNode)
NFS_REPLACE(0x500050, ReturnThis)
NFS_REPLACE(0x500820, ReturnThisPopOne)
NFS_REPLACE(0x4fee70, UpdateInterestPoints)

namespace
{
/** The emitters' random seed for their on and off times. */
unsigned int *const kEmitterSeed = reinterpret_cast<unsigned int *>(0x8f3214);
const float kAngleScale = 65536.0f;

unsigned short Angle(float degrees)
{
    return static_cast<unsigned short>(static_cast<int>(degrees * kAngleScale) / 360);
}

/** An emitter's attributes (Emitter +0x7c, its data at +8). */
const char *Attributes(const void *emitter)
{
    const char *data = *reinterpret_cast<const char *const *>(static_cast<const char *>(emitter) + 0x7c);
    return *reinterpret_cast<const char *const *>(data + 8);
}

float AttributeFloat(const char *a, int offset)
{
    return *reinterpret_cast<const float *>(a + offset);
}

enum ControlState
{
    kStart = 1,
    kDelay = 2,
    kOn = 3,
    kOff = 4,
    kAlwaysOn = 5,
    kDone = 6,
};
} // namespace

void Emitter::GetInitialParticleColorAndSize(const bMatrix4 *sizeBasis, const bMatrix4 *colourBasis, EmitterParticle *particleObject) const
{
    char *particle = reinterpret_cast<char *>(particleObject);
    bVector4 size;
    hermite_parameter(&size, sizeBasis, 0.0f);
    unsigned short &angle = *reinterpret_cast<unsigned short *>(particle + 0x6a);
    *reinterpret_cast<float *>(particle + 0xc) = size.x;
    const int turn = static_cast<int>(size.y);
    // The angle's lowest bit says which way the particle turns.
    angle = static_cast<unsigned short>(angle & 1 ? angle - turn : angle + turn);
    bVector4 colour;
    hermite_parameter(&colour, colourBasis, 0.0f);
    float c[4] = {colour.x * 255.0f, colour.y * 255.0f, colour.z * 255.0f, colour.w * 255.0f};
    for (float &v : c)
    {
        if (v < 0.0f)
            v = 0.0f;
        if (v > 255.0f)
            v = 255.0f;
    }
    *reinterpret_cast<unsigned int *>(particle + 8) = static_cast<unsigned int>(static_cast<int>(c[3])) << 24 |
                                                      static_cast<unsigned int>(static_cast<int>(c[0])) << 16 |
                                                      static_cast<unsigned int>(static_cast<int>(c[1])) << 8 |
                                                      static_cast<unsigned int>(static_cast<int>(c[2]));
}

void Emitter::GetDiscVelocity(float &x, float &y, float &z, unsigned int &seed) const
{
    const float range = AttributeFloat(Attributes(this), 0xf8);
    bMatrix4 local = *reinterpret_cast<const bMatrix4 *>(reinterpret_cast<const char *>(this) + 0x20);
    float *m = reinterpret_cast<float *>(&local);
    m[12] = m[13] = m[14] = 0.0f;
    m[15] = 1.0f;
    const float tilt = bRandom(range, &seed) - range * 0.5f;
    const unsigned short around = Angle(bRandom(360.0f, &seed) - 180.0f);
    bVector3 v = {bCos(around), bSin(around), 0.0f};
    bMatrix4 turn;
    eRotateY(&turn, &eMathIdentityMatrix, Angle(tilt));
    eMulVector(&v, &turn, &v);
    eMulVector(&v, &local, &v);
    x = v.x;
    y = v.y;
    z = v.z;
}

void Emitter::GetConeVelocity(float &x, float &y, float &z, unsigned int &seed) const
{
    const float range = AttributeFloat(Attributes(this), 0xf8);
    const float half = range * 0.5f;
    const float rx = bRandom(range, &seed) - half;
    const float ry = bRandom(range, &seed) - half;
    const float rz = bRandom(range, &seed) - half;
    bMatrix4 turn;
    eRotateX(&turn, &eMathIdentityMatrix, Angle(rx));
    eRotateY(&turn, &turn, Angle(ry));
    eRotateZ(&turn, &turn, Angle(rz));
    bVector3 v;
    eMulVector(&v, &turn, reinterpret_cast<const bVector3 *>(reinterpret_cast<const char *>(this) + 0x40));
    x = v.x;
    y = v.y;
    z = v.z;
}

void __cdecl GetAnimatedUVs(EffectParticleAnimation animation, int frame, unsigned int *first, unsigned int *second)
{
    const int cells = static_cast<int>(animation);
    if (!cells)
        return;
    // Each word holds a corner's two coordinates in 1/65535 steps; the grid has `cells` cells a side.
    const float k = 1.5259022e-05f;
    const double u0 = static_cast<double>(*first >> 16) * k;
    const float v0 = static_cast<float>(*first & 0xffff) * k;
    const float cell = static_cast<float>((static_cast<double>(*second >> 16) * k - u0) / cells);
    const int column = frame % cells, row = frame / cells;
    auto word = [cell](int index, double origin) {
        return static_cast<unsigned int>(static_cast<int>((static_cast<double>(index) * cell + origin) * 65535.0));
    };
    *first = word(column, u0) << 16 | word(row, v0);
    *second = word(column + 1, u0) << 16 | word(row + 1, v0);
}

WorldFXTrigger *EmitterLibraryHeader::GetLibraryTriggers(int index)
{
    char *entry = reinterpret_cast<char *>(this) + 0x10;
    for (int i = 0; i <= index; ++i)
    {
        const unsigned short count = *reinterpret_cast<const unsigned short *>(entry + 0xa);
        if (i == index)
            return count ? reinterpret_cast<WorldFXTrigger *>(entry + 0x50) : nullptr;
        entry += 0x50 + count * 0x30;
    }
    return nullptr;
}

void EmitterLibraryHeader::EndianSwap()
{
    int *self = reinterpret_cast<int *>(this);
    if (self[0])
        return;
    // The PC's data is already in its own order: the walk over the entries is all that is left.
    self[0] = 1;
}

/** EmitterSystem::UpdateInterestPoints (private in the original, so it is replaced by address). */
void __fastcall UpdateInterestPoints(EmitterSystem *system, void *)
{
    char *self = reinterpret_cast<char *>(system);
    const char *const cameras[2] = {*reinterpret_cast<const char *const *>(0x919690), *reinterpret_cast<const char *const *>(0x919700)};
    for (int i = 0; i < 2; ++i)
    {
        const char *c = cameras[i];
        if (!c)
            continue;
        float *position = reinterpret_cast<float *>(self + 0x320 + i * 0x10);
        float *direction = reinterpret_cast<float *>(self + 0x340 + i * 0x10);
        for (int k = 0; k < 3; ++k)
        {
            position[k] = reinterpret_cast<const float *>(c + 0x40)[k];
            direction[k] = reinterpret_cast<const float *>(c + 0x50)[k];
        }
    }
}

void EmitterGroup::SubscribeToDeletion(void *owner, void(__cdecl *callback)(void *, EmitterGroup *))
{
    char *self = reinterpret_cast<char *>(this);
    *reinterpret_cast<void **>(self + 0x60) = owner;
    *reinterpret_cast<void(__cdecl **)(void *, EmitterGroup *)>(self + 0x6c) = callback;
}

void EmitterGroup::UnSubscribe()
{
    char *self = reinterpret_cast<char *>(this);
    *reinterpret_cast<void **>(self + 0x60) = nullptr;
    *reinterpret_cast<void **>(self + 0x6c) = nullptr;
}

bool EmitterControl::Update(float dt, Emitter *emitter, float &overshoot)
{
    int &state = *reinterpret_cast<int *>(this);
    float &timer = *reinterpret_cast<float *>(reinterpret_cast<char *>(this) + 4);
    if (!EnableParticleSystem || !(*reinterpret_cast<const unsigned int *>(reinterpret_cast<const char *>(emitter) + 0x18) >> 4 & 1))
        return false;
    overshoot = 0.0f;
    const char *a = Attributes(emitter);
    float delay = AttributeFloat(a, 0xf4);
    if (a[0x113])
        delay = bRandom(delay);
    const float onTime = AttributeFloat(a, 0xb8), onRandom = AttributeFloat(a, 0xcc);
    const float offTime = AttributeFloat(a, 0xd8), offRandom = AttributeFloat(a, 0xd0);
    const bool oneShot = a[0x112] || (*reinterpret_cast<const unsigned int *>(reinterpret_cast<const char *>(emitter) + 0x18) >> 22 & 1);
    auto nonZero = [](float v) { return v > 0.0f || v < 0.0f; };
    const bool hasDelay = nonZero(delay);
    bool hasOn = nonZero(onTime) || nonZero(onRandom);
    const bool hasOff = nonZero(offTime) || nonZero(offRandom);
    if (hasOn && !oneShot && !hasOff)
        hasOn = false;
    auto startOn = [&](float carried) {
        state = kOn;
        timer = bRandom(onRandom, kEmitterSeed) + onTime + carried;
    };
    switch (state)
    {
    case kStart:
        if (hasDelay)
        {
            state = kDelay;
            timer = delay - dt;
            return false;
        }
        if (hasOn)
        {
            state = kOn;
            timer = bRandom(onRandom, kEmitterSeed) + onTime - dt;
            if (timer < 0.0f)
                timer = 0.0f;
            return true;
        }
        state = kAlwaysOn;
        timer = 0.0f;
        return true;
    case kDelay:
        timer -= dt;
        if (timer > 0.0f)
            return false;
        overshoot = timer;
        if (hasOn)
        {
            startOn(timer);
            return true;
        }
        state = kAlwaysOn;
        timer = 0.0f;
        return true;
    case kOn:
        timer -= dt;
        if (timer > 0.0f)
            return true;
        overshoot = timer + dt;
        if (oneShot)
        {
            state = kDone;
            timer = 0.0f;
            return true;
        }
        state = kOff;
        timer = bRandom(offRandom, kEmitterSeed) + offTime + timer;
        return true;
    case kOff:
        timer -= dt;
        if (timer > 0.0f)
            return false;
        overshoot = timer;
        startOn(timer);
        return true;
    case kAlwaysOn:
        return true;
    default:
        return false;
    }
}

int __fastcall FirstWord(const int *self, void *)
{
    return *self;
}

/** A bList iterator's node, taken out of its list (thiscall on the iterator). */
void __fastcall UnlinkListNode(int **iterator, void *)
{
    int *node = *iterator;
    int *next = reinterpret_cast<int *>(node[0]), *previous = reinterpret_cast<int *>(node[1]);
    *previous = reinterpret_cast<int>(next);
    next[1] = reinterpret_cast<int>(previous);
}

void *__fastcall ReturnThis(void *self, void *)
{
    return self;
}

void *__fastcall ReturnThisPopOne(void *self, void *, int)
{
    return self;
}
