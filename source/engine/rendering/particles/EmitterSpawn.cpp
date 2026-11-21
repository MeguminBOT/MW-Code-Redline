/**
 * A frame of particle spawning (original: zEcstasy Emitter::SpawnParticles). The emitter data (Attrib layout, wrapper
 * +8) gives ranges; for each spawned particle random values are drawn from the emitter's seed (+0x14) in a fixed order,
 * so that an emitter plays back the same way each time.
 *
 * Emitter data offsets: +0x00..0x0c velocity start (with +0x30.. box range), +0x10.. and +0x70.. spin range and start,
 * +0x60 and +0x80 position range and start, +0x90 animation frames (per row), +0x95 random start frame, +0xa4 rotation
 * spread (degrees), +0xbc life, +0xc0 rate jitter, +0xc4 spawn rate per emitter-age unit, +0xdc alpha, +0xe0 no inherited
 * velocity, +0xe8 life jitter, +0xec inherited velocity amount, +0xf0 speed, +0xfc random sprite flip, +0x100 speed
 * jitter, +0x104 inherited velocity jitter, +0x10c velocity from a disc rather than a cone, +0x114 box velocity is absolute.
 * Emitter: +0x10 fractional spawn accumulator, +0x20 local world, +0x60 inherited velocity, +0x70/+0x74 life range.
 * Particle: +0x10 velocity, +0x30 spin, +0x50 position, +0x60 u, +0x64 v, +0x68 life (1/1024 s), +0x6a rotation (low bit:
 * flip), +0x6c frame, +0x6e rotation byte, +0x6f alpha.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

const int kMaxParticles = 0x400;
const float kMaxLife = 60.0f;

/** Scale 0..1 drawn for how much of a value applies. */
float Clamp01(float value)
{
    if (!(value > 0.0f))
        return 0.0f;
    return 1.0f < value ? 1.0f : value;
}
} // namespace

void Emitter::SpawnParticles(float dt, float age)
{
    char *self = reinterpret_cast<char *>(this);
    const bool inNis = IsInNIS;
    if (age <= 0.0f || age > At<float>(self, 0x74) || age < At<float>(self, 0x70))
        return;

    unsigned int seed = At<unsigned int>(self, 0x14);
    char *wrapper = At<char *>(self, 0x7c);
    char *data = At<char *>(wrapper, 8);
    const float *d = reinterpret_cast<const float *>(data);
    const float rotationSpread = At<float>(data, 0xa4) * 182.04445f;
    const float rotationOffset = rotationSpread * -0.5f;
    const float lifeRange = At<float>(data, 0xe8) * At<float>(data, 0xbc);
    const float inheritRange = At<float>(data, 0x104) * At<float>(data, 0xec);
    const float speedRange = At<float>(data, 0x100) * At<float>(data, 0xf0);
    const float lifeBase = At<float>(data, 0xbc) - lifeRange;
    const float inheritBase = At<float>(data, 0xec) - inheritRange;
    const float speedBase = At<float>(data, 0xf0) - speedRange;
    const int flipSprites = At<int>(data, 0xfc);
    const int fromDisc = At<int>(data, 0x10c);
    const bool noInherit = At<int>(data, 0xe0) != 0;
    const bool absoluteBox = At<char>(data, 0x114) != 0;
    const int animationRows = At<int>(data, 0x90);
    const bool randomFrame = At<char>(data, 0x95) != 0;

    const bool perAge = (At<unsigned char>(self, 0x18) & 1) || (At<unsigned int>(self, 0x18) & 0x400000);
    const bool hasVelocitySpread = At<float>(data, 0xb8) != 0.0f || At<float>(data, 0xcc) != 0.0f;
    const float rate = age * At<float>(data, 0xc4);
    const float amount = (rate - At<float>(data, 0xc0) * rate) * (perAge && !hasVelocitySpread ? lifeBase : dt);

    int spawnCount = static_cast<int>(amount);
    if (!spawnCount)
    {
        const float accumulated = amount + At<float>(self, 0x10);
        At<float>(self, 0x10) = accumulated;
        if (accumulated > 1.0f)
        {
            spawnCount = 1;
            At<float>(self, 0x10) = accumulated - 1.0f;
        }
    }
    if (spawnCount <= 0)
    {
        At<unsigned int>(self, 0x14) = seed;
        return;
    }

    const float halfX = At<float>(data, 0x60) * 0.5f, halfY = At<float>(data, 0x64) * 0.5f, halfZ = At<float>(data, 0x68) * 0.5f;
    for (; spawnCount; --spawnCount)
    {
        float velocity[3] = {0.0f, 0.0f, 0.0f};
        float spin[3] = {0.0f, 0.0f, 0.0f};
        const float speed = bRandom(speedRange, &seed) + speedBase;
        if (speedBase > 0.0f || speedBase < 0.0f)
        {
            float x, y, z;
            if (fromDisc)
                GetDiscVelocity(x, y, z, seed);
            else
                GetConeVelocity(x, y, z, seed);
            velocity[0] = x * speed;
            velocity[1] = y * speed;
            velocity[2] = z * speed;
        }
        else if (absoluteBox)
        {
            const float r1 = bRandom(At<float>(data, 0x38), &seed);
            const float r2 = bRandom(At<float>(data, 0x34), &seed);
            const float r3 = bRandom(At<float>(data, 0x30), &seed);
            const float r4 = bRandom(At<float>(data, 0x18), &seed);
            const float r5 = bRandom(At<float>(data, 0x14), &seed);
            const float r6 = bRandom(At<float>(data, 0x10), &seed);
            velocity[0] = r3 + d[0];
            velocity[1] = r2 + d[1];
            velocity[2] = r1 + d[2];
            spin[0] = r6 + At<float>(data, 0x70);
            spin[1] = r5 + At<float>(data, 0x74);
            spin[2] = r4 + At<float>(data, 0x78);
        }
        else
        {
            float r = bRandom(At<float>(data, 0x38), &seed);
            const float alongZ = r + r + (d[2] - At<float>(data, 0x38));
            velocity[0] = alongZ * At<float>(self, 0x40);
            velocity[1] = alongZ * At<float>(self, 0x44);
            velocity[2] = alongZ * At<float>(self, 0x48);
            r = bRandom(At<float>(data, 0x34), &seed);
            const float alongY = r + r + (d[1] - At<float>(data, 0x34));
            velocity[0] += alongY * At<float>(self, 0x30);
            velocity[1] += alongY * At<float>(self, 0x34);
            velocity[2] += alongY * At<float>(self, 0x38);
            r = bRandom(At<float>(data, 0x30), &seed);
            const float alongX = r + r + (d[0] - At<float>(data, 0x30));
            velocity[0] += alongX * At<float>(self, 0x20);
            velocity[1] += alongX * At<float>(self, 0x24);
            velocity[2] += alongX * At<float>(self, 0x28);
            r = bRandom(At<float>(data, 0x10), &seed);
            spin[0] = r + r + (At<float>(data, 0x70) - At<float>(data, 0x10));
            r = bRandom(At<float>(data, 0x14), &seed);
            spin[1] = r + r + (At<float>(data, 0x74) - At<float>(data, 0x14));
            r = bRandom(At<float>(data, 0x18), &seed);
            spin[2] = r + r + (At<float>(data, 0x78) - At<float>(data, 0x18));
        }

        bVector4 position;
        position.x = bRandom(At<float>(data, 0x60), &seed) - halfX + At<float>(data, 0x80);
        position.y = bRandom(At<float>(data, 0x64), &seed) - halfY + At<float>(data, 0x84);
        position.z = bRandom(At<float>(data, 0x68), &seed) - halfZ + At<float>(data, 0x88);
        position.w = 1.0f;
        eMulVector(&position, reinterpret_cast<const bMatrix4 *>(self + 0x20), &position);

        float life = bRandom(lifeRange, &seed) + lifeBase;
        const float rotation = bRandom(rotationSpread, &seed) + rotationOffset;

        if (!(inheritBase == 0.0f))
        {
            const float amountInherited = Clamp01(bRandom(inheritRange, &seed) + inheritBase);
            if (!noInherit)
                bScaleAdd(reinterpret_cast<bVector3 *>(velocity), reinterpret_cast<const bVector3 *>(velocity),
                          reinterpret_cast<const bVector3 *>(self + 0x60), amountInherited);
        }

        unsigned short rotationBits = static_cast<unsigned short>(static_cast<int>(rotation)) & 0xfffe;
        const unsigned char rotationByte =
            static_cast<unsigned char>(static_cast<int>(static_cast<float>(static_cast<int>(rotationBits)) * 1.5259022e-05f * 255.0f));
        if (flipSprites)
            rotationBits |= static_cast<unsigned short>(seed & 1);

        if (life > kMaxLife)
            life = kMaxLife;

        if (!inNis && At<int>(&gEmitterSystem, 0) >= kMaxParticles)
            continue;
        char *particle = reinterpret_cast<char *>(gEmitterSystem.GetNewParticle(this));
        if (!particle)
            continue;

        ++At<short>(self, 0x1c);
        At<float>(particle, 0x50) = position.x;
        At<float>(particle, 0x54) = position.y;
        At<float>(particle, 0x58) = position.z;
        At<float>(particle, 0x10) = velocity[0];
        At<float>(particle, 0x14) = velocity[1];
        At<float>(particle, 0x18) = velocity[2];
        At<float>(particle, 0x30) = spin[0];
        At<float>(particle, 0x34) = spin[1];
        At<float>(particle, 0x38) = spin[2];
        At<short>(particle, 0x68) = static_cast<short>(static_cast<int>(life * 1024.0f));
        At<unsigned char>(particle, 0x6e) = rotationByte;
        At<unsigned short>(particle, 0x6a) = rotationBits;
        At<unsigned char>(particle, 0x6f) = static_cast<unsigned char>(static_cast<int>(Clamp01(bRandom(At<float>(data, 0xdc), &seed)) * 255.0f));

        unsigned int u = 0, v = 0xffffffff;
        if (animationRows)
        {
            const float lastFrame = static_cast<float>(animationRows * animationRows) - 1.0f;
            float frame = 0.0f;
            if (randomFrame)
                frame = bRandom(lastFrame);
            At<short>(particle, 0x6c) = static_cast<short>(static_cast<int>(frame / lastFrame * 65535.0f));
            GetAnimatedUVs(static_cast<EffectParticleAnimation>(animationRows), static_cast<int>(frame), &u, &v);
        }
        At<unsigned int>(particle, 0x60) = u;
        At<unsigned int>(particle, 0x64) = v;
        GetInitialParticleColorAndSize(reinterpret_cast<const bMatrix4 *>(wrapper + 0x60),
                                       reinterpret_cast<const bMatrix4 *>(wrapper + 0x20),
                                       reinterpret_cast<EmitterParticle *>(particle));
    }
    At<unsigned int>(self, 0x14) = seed;
}
