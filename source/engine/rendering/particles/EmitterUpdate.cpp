/**
 * The particle update (original: zEcstasy EmitterSystem::UpdateParticles 0x508c30), once per frame for every particle of
 * every emitter: groups out of range lose their particles; the others' particles age by the frame's time, move (drag,
 * gravity or their own acceleration), advance their texture animation, and take their size, spin and colour from the
 * emitter's curves over their life. Particles that run out of life, or fade below the emitter's kill alpha, are freed.
 *
 * The emitter's curve products (UMath::Mult 0x4fa510) and the vector steps (bScaleAdd) are inlined, in the original's
 * order of operations.
 */
#include <math.h>

#include "game/ecstasy/Particles.h"
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x508c30, EmitterSystem::UpdateParticles)

namespace
{
namespace pt = nfs::particles;

/** Groups with this flag update wherever they are. */
const unsigned int kGroupAlwaysUpdates = 0x20000;
const float kNearEnoughScale = 0.7f;
const float kTicksPerSecond = 1024.0f;
const float kPhaseScale = 65535.0f;
const float kInversePhaseScale = 1.5259022e-05f;
const float kInverseByte = 0.003921569f;
const float kColourScale = 255.0f;

/** UMath::Mult: the row vector v times the 4x4 matrix m, summed from the last row up as the original does. */
void Mult(const float v[4], const float *m, float out[4])
{
    for (int j = 0; j < 4; ++j)
        out[j] = static_cast<float>(static_cast<double>(v[3]) * m[12 + j] + static_cast<double>(v[2]) * m[8 + j] +
                                    static_cast<double>(v[1]) * m[4 + j] + static_cast<double>(v[0]) * m[j]);
}

/** a + b * scale per component (bScaleAdd). */
void ScaleAdd(float out[3], const float a[3], const float b[3], float scale)
{
    for (int i = 0; i < 3; ++i)
        out[i] = static_cast<float>(static_cast<double>(scale) * b[i] + a[i]);
}

int ClampByte(float v)
{
    const int i = static_cast<int>(v);
    return i <= 0 ? 0 : i > 0xff ? 0xff : i;
}

void FreeParticle(pt::System &system, pt::Emitter &emitter, pt::Particle *particle)
{
    --system.typeCounts[emitter.type];
    --system.particleCount;
    --emitter.particleCount;
    particle->prev->next = particle->next;
    particle->next->prev = particle->prev;
    bFree(ParticleSlotPool, particle);
}
} // namespace

void EmitterSystem::UpdateParticles(float dt)
{
    pt::System &system = *reinterpret_cast<pt::System *>(this);
    if (!EnableParticleSystem || !system.particleCount)
        return;
    const int ticks = static_cast<int>(static_cast<double>(dt) * kTicksPerSecond);
    pt::Group *const groups = reinterpret_cast<pt::Group *>(&system.firstGroup);
    const pt::Data *cached = nullptr;
    float drag = 0.0f, gravity = 0.0f, lifetime = 0.0f, frames = 0.0f;
    int animation = 0;
    const float *sizeCurve = nullptr, *colorCurve = nullptr;
    for (pt::Group *group = system.firstGroup; group != groups; group = group->next)
    {
        pt::Emitter *const emitters = reinterpret_cast<pt::Emitter *>(&group->first);
        if (!(group->flags & kGroupAlwaysUpdates) && !IsCloseEnough(reinterpret_cast<const bVector3 *>(group->position), group->radius, 0, kNearEnoughScale))
        {
            for (pt::Emitter *emitter = group->first; emitter != emitters; emitter = emitter->next)
                gEmitterSystem.KillParticlesFromThisEmitter(reinterpret_cast<::Emitter *>(emitter));
            continue;
        }
        for (pt::Emitter *emitter = group->first; emitter != emitters; emitter = emitter->next)
        {
            pt::Particle *const end = reinterpret_cast<pt::Particle *>(&emitter->first);
            pt::Particle *particle = emitter->first;
            if (particle == end)
                continue;
            if (emitter->data != cached)
            {
                cached = emitter->data;
                const pt::Attributes &a = *cached->attributes;
                drag = a.drag;
                gravity = a.gravity;
                lifetime = a.lifetime;
                sizeCurve = cached->sizeCurve;
                colorCurve = cached->colorCurve;
                animation = a.animation;
                frames = static_cast<float>(a.animationFrames);
            }
            while (particle != end)
            {
                pt::Particle *next = particle->next;
                pt::Particle &p = *particle;
                if (static_cast<int>(p.life) <= ticks)
                {
                    FreeParticle(system, *emitter, particle);
                    particle = next;
                    continue;
                }
                if (animation)
                {
                    const double cells = static_cast<double>(static_cast<unsigned int>(animation * animation));
                    const float lastCell = static_cast<float>(cells - 1.0);
                    const int step = static_cast<int>(static_cast<double>(frames) * dt / cells * kPhaseScale);
                    unsigned int phase = p.phase + step;
                    if (static_cast<unsigned int>(step) + phase > 0xffff)
                        phase -= 0xffff;
                    phase &= 0xffff;
                    p.uv0 = 0;
                    p.uv1 = 0xffffffff;
                    const int frame = static_cast<int>(static_cast<double>(phase) * kInversePhaseScale * lastCell);
                    GetAnimatedUVs(static_cast<EffectParticleAnimation>(animation), frame, &p.uv0, &p.uv1);
                    p.phase = static_cast<unsigned short>(phase);
                }
                float velocity[3] = {p.velocity[0], p.velocity[1], p.velocity[2]};
                if (drag > 0.0f)
                {
                    const double speed = sqrt(static_cast<double>(velocity[0]) * velocity[0] +
                                              static_cast<double>(velocity[2]) * velocity[2] +
                                              static_cast<double>(velocity[1]) * velocity[1]);
                    float k = static_cast<float>(-(speed * drag * dt));
                    if (k < -1.0f)
                        k = -1.0f;
                    ScaleAdd(velocity, velocity, velocity, k);
                }
                if (gravity < 0.0f || gravity > 0.0f)
                    velocity[2] = static_cast<float>(velocity[2] - static_cast<double>(gravity) * dt);
                else
                    ScaleAdd(velocity, velocity, p.acceleration, dt);
                ScaleAdd(p.position, p.position, velocity, dt);
                p.velocity[0] = velocity[0];
                p.velocity[1] = velocity[1];
                p.velocity[2] = velocity[2];
                p.life = static_cast<unsigned short>(p.life - ticks);

                const double t = 1.0 - static_cast<double>(p.life) / (static_cast<double>(lifetime) * kTicksPerSecond);
                const double t2 = t * t;
                const float age[4] = {static_cast<float>(t2 * t), static_cast<float>(t2), static_cast<float>(t), 1.0f};
                float size[4];
                Mult(age, sizeCurve, size);
                p.size = size[0];
                int rotation = static_cast<int>(static_cast<double>(p.spin) * kInverseByte * kPhaseScale);
                const int spin = static_cast<int>(static_cast<double>(p.spinSpeed) * kInverseByte * size[1] + size[1]);
                rotation = rotation & 1 ? rotation - spin : rotation + spin;
                p.rotation = static_cast<unsigned short>(rotation);

                float colour[4];
                Mult(age, colorCurve, colour);
                const int r = ClampByte(colour[0] * kColourScale);
                const int g = ClampByte(colour[1] * kColourScale);
                const int b = ClampByte(colour[2] * kColourScale);
                const unsigned int alpha = static_cast<unsigned int>(ClampByte(colour[3] * kColourScale));
                p.color = alpha << 24 | static_cast<unsigned int>(r) << 16 | static_cast<unsigned int>(g) << 8 |
                          static_cast<unsigned int>(b);
                const pt::Attributes &a = *emitter->data->attributes;
                if (!a.keepFaded && alpha <= static_cast<unsigned int>(static_cast<int>(a.killAlpha)))
                    FreeParticle(system, *emitter, particle);
                particle = next;
            }
        }
    }
}
