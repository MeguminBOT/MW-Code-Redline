/**
 * Scenery the game changes while playing, for the sun shadows' static layers (engine/rendering/culling/culling.h InvalidateStaticCasters):
 * a smackable prop knocked over (its scenery instance is hidden and a render connection draws it from then on), and the
 * scenery groups that events show and hide (barriers, blocked roads). Each redraws the static layers, which would
 * otherwise keep the old shadows until the cascade next moved.
 */
#include "game/gen/api.h"
#include "engine/rendering/culling/culling.h"
#include "engine/rendering/lights/lamps.h"
#include "legacy/link.h"

NFS_REPLACE(0x742170, SmackableRenderConnConstruct)
NFS_REPLACE(0x75abd0, SmackableRenderConnUpdate)

namespace
{
/** The connections made and not yet updated: their first update places the prop where it stood (its body's matrix at
 * +0x24, the model's own matrix copied to +0x50, so its origin at +0x80), where a lamp post's lamp is. */
const int kMaxNew = 32;
void *g_new[kMaxNew];
int g_newCount;
} // namespace

void *__fastcall SmackableRenderConnConstruct(void *self, void *, const void *data)
{
    nfs::culling::InvalidateStaticCasters();
    void *made = NFS_ORIGINAL(0x742170, void *(__thiscall *)(void *, const void *))(self, data);
    g_new[g_newCount++ % kMaxNew] = self;
    return made;
}

void __fastcall SmackableRenderConnUpdate(void *self, void *, float dt)
{
    NFS_ORIGINAL(0x75abd0, void(__thiscall *)(void *, float))(self, dt);
    const int count = g_newCount < kMaxNew ? g_newCount : kMaxNew;
    for (int i = 0; i < count; ++i)
    {
        if (g_new[i] != self)
            continue;
        const unsigned char *conn = static_cast<const unsigned char *>(self);
        if (!*reinterpret_cast<void *const *>(conn + 0x24))
            return;
        g_new[i] = nullptr;
        nfs::lamps::KnockedOver(reinterpret_cast<const float *>(conn + 0x80));
        return;
    }
}

void __cdecl EnableSceneryGroup(unsigned int name, bool flip)
{
    nfs::culling::InvalidateStaticCasters();
    NFS_ORIGINAL(0x72c5c0, void(__cdecl *)(unsigned int, bool))(name, flip);
}

void __cdecl DisableSceneryGroup(unsigned int name)
{
    nfs::culling::InvalidateStaticCasters();
    NFS_ORIGINAL(0x72c620, void(__cdecl *)(unsigned int))(name);
}

void __cdecl DisableAllSceneryGroups()
{
    nfs::culling::InvalidateStaticCasters();
    NFS_ORIGINAL(0x72c660, void(__cdecl *)())();
}
