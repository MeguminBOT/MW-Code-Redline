/**
 * The particles' draw (original: zEcstasy EmitterSystem::Render): every emitter with live particles is one batch, with
 * its texture (the emitter data's +0x98 name hash), the view's vectors to turn its quads to the camera, and for
 * constrained particles (sparks along their motion, the data's +0xc8) a basis scaled by each particle's size.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/log.h"

void __cdecl SetParticleStats(int a, int b, int c, int d, int e, int f, int g, int h);

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

struct Node
{
    Node *next;
    Node *prev;
};

/** A particle flag for PlatAddParticle: the quad comes with its own basis. */
const unsigned int kConstrained = 2;
/** PlatAddParticle (0x6c1160): api.h has it as a __cdecl function, but it is a member of the particle batch's state at
 * 0x9877CC (thiscall, ret 18h: it pops its own arguments). */
void AddParticle(EmitterParticle *particle, UMath::Vector3 &first, UMath::Vector3 &second, unsigned int flags, bVector4 *basisA,
                 bVector4 *basisB)
{
    NFS_ORIGINAL(0x6c1160, void(__thiscall *)(void *, EmitterParticle *, UMath::Vector3 *, UMath::Vector3 *, unsigned int,
                                              bVector4 *, bVector4 *))(reinterpret_cast<void *>(0x9877cc), particle, &first,
                                                                       &second, flags, basisA, basisB);
}

} // namespace

static_assert(sizeof(UMath::Vector3) == 12, "UMath::Vector3 has its fields (legacy/include/game/layouts)");

void EmitterSystem::Render(eView *view)
{
    char *self = reinterpret_cast<char *>(this);
    if (!EnableParticleSystem || At<int>(self, 0) <= 0)
        return;
    if (!view->PlatInfo)
    {
        static int logged;
        if (logged++ < 3)
            nfs::Log("particles: view %d has no platform info (frame %u)", view->ID, eFrameCounter);
        return;
    }
    int batches = 0;
    bMatrix4 viewMatrix = **reinterpret_cast<bMatrix4 **>(view);
    Node *groups = &At<Node>(self, 0x378);
    for (Node *g = groups->next; g != groups; g = g->next)
    {
        Node *emitters = &At<Node>(g, 8);
        for (Node *e = emitters->next; e != emitters; e = e->next)
        {
            Node *particles = &At<Node>(e, 0x80);
            if (particles->next == particles)
                continue;
            ++batches;
            const char *data = *reinterpret_cast<const char *const *>(At<char *>(e, 0x7c) + 8);
            TextureInfo *texture = GetTextureInfo(*reinterpret_cast<const unsigned int *>(data + 0x98), 1, 0);
            At<TextureInfo *>(self, 0x38c) = texture;
            if (!texture)
                continue;
            unsigned int flags = 0;
            UMath::Vector3 right, up, forward;
            PlatGetViewVectors(view, right, up, forward);
            if (PlatStartParticleRender(view, texture, At<unsigned short>(e, 0x1c)))
            {
                for (Node *p = particles->next; p != particles; p = p->next)
                {
                    EmitterParticle *particle = reinterpret_cast<EmitterParticle *>(p);
                    const EffectParticleConstraint constraint =
                        *reinterpret_cast<const EffectParticleConstraint *>(
                            *reinterpret_cast<const char *const *>(At<char *>(e, 0x7c) + 8) + 0xc8);
                    bVector4 basis[2];
                    const bool constrained = constraint != 0;
                    if (constrained)
                    {
                        GetConstraintBasis(constraint, basis[0], basis[1], At<unsigned short>(p, 0x6a), &viewMatrix);
                        const float size = At<float>(p, 0xc);
                        for (bVector4 &b : basis)
                        {
                            b.x *= size;
                            b.y *= size;
                            b.z *= size;
                            b.w *= size;
                        }
                        flags |= kConstrained;
                    }
                    UMath::Vector3 a, b;
                    PlatRotateScaleParticle(particle, right, up, forward, a, b);
                    AddParticle(particle, b, a, flags, constrained ? &basis[0] : nullptr, constrained ? &basis[1] : nullptr);
                }
            }
            PlatEndParticleRender();
        }
    }
    SetParticleStats(At<int>(self, 0), 0x400, batches, 0x2e, At<int>(self, 0x394), 500, At<int>(self, 0x390), 200);
}
