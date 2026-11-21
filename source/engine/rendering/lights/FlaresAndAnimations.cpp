/**
 * The world's light flares and the animated textures (original: zEcstasy): each frame's flares of the track sections the
 * camera's drivable section can see, the flare types' parameters from the attribute database (22 types, colour,
 * size and fade figures), and the textures that flip through frames (neon signs, screens) at their frame rates.
 */
#include <new>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/log.h"
#include "engine/rendering/vulkan/renderer.h"

NFS_REPLACE(0x5025c0, FlareParameterInstance)

extern "C" void *ScenerySectionHeaders[];

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

/** The flare types' attribute collections (class 0xc7c5806d), in their table's order (0x8f2498, 0x2c each). */
const unsigned int kFlareTypes[22] = {0x356f692a, 0xa95ab5ad, 0xb60dac69, 0xd364e8ac, 0x30ea58b4, 0x35ce359c, 0xf51bf17f, 0xdde4b816,
                                      0x44c51b58, 0x93007a08, 0x0299e454, 0x47cb2ad7, 0xe9faf022, 0x4ff2b81c, 0x96147a22, 0x37b7c081,
                                      0x8412720e, 0x1e7fdc6e, 0x20fc956e, 0xd06fc6bb, 0xbf79f943, 0x55eaa808};
const unsigned int kFlareClass = 0xc7c5806d;
float *const kFlareTable = reinterpret_cast<float *>(0x8f2498);
const int kFlareEntryFloats = 0x2c / 4;
} // namespace

/** The flare parameters' attribute instance (original sub_5025C0, thiscall): its collection by key, or the defaults. */
void *__fastcall FlareParameterInstance(void *self, void *, unsigned int key, unsigned int msgPort, UTL::COM::IUnknown *owner)
{
    new (self) Attrib::Instance(Attrib::FindCollection(kFlareClass, key), msgPort, owner);
    if (!At<const void *>(self, 8))
        At<const void *>(self, 8) = Attrib::DefaultDataArea(0x2c);
    return self;
}

void __cdecl UpdateLightFlareParameters()
{
    int &needed = *reinterpret_cast<int *>(0x8f2860);
    if (!needed || !*reinterpret_cast<void **>(0x90dcbc))
        return;
    needed = 0;
    for (int i = 0; i < 22; ++i)
    {
        alignas(4) unsigned char instance[0x14];
        FlareParameterInstance(instance, nullptr, kFlareTypes[i], 0, nullptr);
        const float *l = *reinterpret_cast<const float *const *>(instance + 8);
        float *e = kFlareTable + i * kFlareEntryFloats;
        e[0] = l[4];
        e[1] = l[9];
        e[2] = l[0] * 255.0f;
        e[3] = l[1] * 255.0f;
        e[4] = l[2] * 255.0f;
        e[5] = l[3] * 255.0f;
        e[6] = l[6];
        e[7] = l[10];
        e[8] = l[5];
        e[9] = l[8];
        e[10] = l[7];
        reinterpret_cast<Attrib::Instance *>(instance)->~Instance();
    }
}

void __cdecl eRenderWorldLightFlares(eView *view, flareType type)
{
    if (!DrawLightFlares || *reinterpret_cast<const int *>(0x925e90) != 6)
        return;
    const bVector2 *camera = reinterpret_cast<const bVector2 *>(At<char *>(view, 0x40) + 0x40);
    char *drivable = reinterpret_cast<char *>(TheVisibleSectionManager.FindDrivableSection(camera));
    static const bool debug = nfs::vk::HasArgument("-lightdebug");
    static int lastDrawn = -1, lastSection = -1;
    const bool player = view->ID == 1;
    if (!drivable)
    {
        if (debug && player && lastDrawn != 0)
            nfs::Log("world flares: camera %d %d over no drivable section: none drawn (were %d)", static_cast<int>(camera->x),
                     static_cast<int>(camera->y), lastDrawn);
        if (player)
            lastDrawn = 0;
        return;
    }
    const short count = At<short>(drivable, 0x10);
    const short *sections = &At<short>(drivable, 0x12);
    int drawn = 0;
    for (int i = 0; i < count; ++i)
    {
        char *header = static_cast<char *>(ScenerySectionHeaders[sections[i]]);
        char *flares = header ? At<char *>(header, 8) : nullptr;
        if (!flares)
            continue;
        if (!reinterpret_cast<eViewPlatInterface *>(view)->GetVisibleStateSB(&At<bVector3>(flares, 0x30), &At<bVector3>(flares, 0x40),
                                                                             nullptr))
            continue;
        char *head = flares + 0x58;
        for (char *f = At<char *>(head, 0); f != head; f = At<char *>(f, 0))
        {
            eRenderLightFlare(view, reinterpret_cast<eLightFlare *>(f), &eMathIdentityMatrix, 1.0f, static_cast<eLightReflexionType>(0),
                              type, 0.0f, 0, 1.0f);
            ++drawn;
        }
    }
    if (debug && player)
    {
        const int section = At<short>(drivable, 0x0c);
        if (section != lastSection || (lastDrawn >= 0 && (drawn * 4 < lastDrawn * 3 || drawn * 3 > lastDrawn * 4)))
            nfs::Log("world flares: camera %d %d over section %d (%d visible): %d flares (were %d)", static_cast<int>(camera->x),
                     static_cast<int>(camera->y), section, count, drawn, lastDrawn);
        lastSection = section;
        lastDrawn = drawn;
    }
}

void __cdecl UpdateTextureAnimations()
{
    const int flow = *reinterpret_cast<const int *>(0x925e90);
    const float elapsed = *reinterpret_cast<const float *>(0x925970);
    if (flow != 3 && elapsed == 0.0f)
        return;
    // Two clocks in sixtieths of a second: the real one, and one that stops with the game (an animation's +0x24 picks).
    const float realSeconds = static_cast<float>(*reinterpret_cast<const int *>(0x8f8310)) * 0.016666668f;
    const float gameSeconds = static_cast<float>(*reinterpret_cast<const int *>(0x8f82f4)) * 0.016666668f;
    char *const list = reinterpret_cast<char *>(0x91c160);
    for (char *pack = At<char *>(list, 0); pack != list; pack = At<char *>(pack, 0))
    {
        char *anim = At<char *>(pack, 0xc);
        for (int n = At<int>(pack, 0x14); n > 0; --n, anim += 0x34)
        {
            if (!At<int>(anim, 0x2c))
                continue;
            const float seconds = At<int>(anim, 0x24) == 1 ? gameSeconds : realSeconds;
            const int frames = At<int>(anim, 0x1c);
            const int frame = static_cast<int>(static_cast<float>(At<int>(anim, 0x20)) * seconds) % frames;
            if (frame == At<int>(anim, 0x30))
                continue;
            At<int>(anim, 0x30) = frame;
            char *table = At<char *>(anim, 0x28);
            for (int k = 0; k < frames; ++k)
                NFS_ORIGINAL(0x6bee20, void(__thiscall *)(void *, void *))(At<void *>(table, k * 0x10 + 4),
                                                                           At<void *>(table, ((k + frame) % frames) * 0x10 + 8));
        }
    }
}
