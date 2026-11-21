/**
 * The rain's sound (original: zEAXSound2 CARSFX_Rain::UpdateParams 0x4dd070): it starts and stops with the camera
 * view's rain, and its thunder (a gameplay moment stream, heavier in rain from 0.5, CARSFX_Rain::QueueWeatherStream)
 * rolled in on a timer of its own, every 25 to 45 s in rain over 0.25. While the weather makes its own lightning
 * (gameplay/world/weather.h OwnThunder) the thunder follows the flashes instead, 0 to 5 s after each.
 *
 * CARSFX_Rain: +0x28 the rain now, +0x2c last update's, +0x30 the thunder timer, +0x34 its period, +0x38 a moment
 * stream queued, +0x3c the timer's count, +0x40 the fade's time left, +0x44 fading out, +0x48 the playing sound.
 * Its vtable: +0x20 the fade's end (stop), +0x34 QueueWeatherStream.
 */
#include "game/gen/api.h"
#include "gameplay/world/weather.h"
#include "legacy/link.h"

NFS_REPLACE(0x4dd070, RainSoundUpdateParams)

void __fastcall RainSoundUpdateParams(char *self, void *, float dt);

namespace
{
template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

const int kRain = 0x28;
const int kLastRain = 0x2c;
const int kTimer = 0x30;
const int kPeriod = 0x34;
const int kQueued = 0x38;
const int kCount = 0x3c;
const int kFadeLeft = 0x40;
const int kFading = 0x44;
const int kPlaying = 0x48;
const int kVirtualFadeEnd = 0x20;
const int kVirtualQueueStream = 0x34;
char *const *const kCameraRain = reinterpret_cast<char *const *>(0x9196b8);
const int kRainIntensity = 0x28c;

void Virtual(char *self, int offset)
{
    using Fn = void(__thiscall *)(void *);
    reinterpret_cast<Fn>((*reinterpret_cast<void ***>(self))[offset / 4])(self);
}

bool Streaming()
{
    return NFS_ORIGINAL(0x4b1dd0, bool(__cdecl *)(int))(0);
}

/** The thunder's moment stream, as the timer started it: the queued flag cleared first, so it can play again. */
void QueueThunder(char *self)
{
    if (At<char>(self, kQueued) == 1)
        At<char>(self, kQueued) = 0;
    Virtual(self, kVirtualQueueStream);
}
} // namespace

void __fastcall RainSoundUpdateParams(char *self, void *, float dt)
{
    At<unsigned int>(self, kLastRain) = At<unsigned int>(self, kRain);
    const float rain = *kCameraRain ? At<float>(*kCameraRain, kRainIntensity) : 0.0f;
    At<float>(self, kRain) = rain;
    if (rain > 0.01f)
    {
        if (!At<void *>(self, kPlaying))
            NFS_ORIGINAL(0x4dce50, void(__thiscall *)(void *))(self);
    }
    else if (At<void *>(self, kPlaying) && !At<char>(self, kFading))
        NFS_ORIGINAL(0x4dcf30, void(__thiscall *)(void *))(self);
    At<float>(self, kTimer) = dt + At<float>(self, kTimer);
    if (nfs::weather::OwnThunder())
    {
        if (nfs::weather::ThunderDue() && !Streaming())
        {
            QueueThunder(self);
            if (At<char>(self, kQueued) == 1)
                nfs::weather::ThunderPlayed();
        }
    }
    else if (rain > 0.25f && !Streaming() && At<float>(self, kTimer) > At<float>(self, kPeriod))
    {
        if (At<int>(self, kCount) == 0)
        {
            At<float>(self, kTimer) = 0.0f;
            QueueThunder(self);
            const unsigned int extra =
                NFS_ORIGINAL(0x4aecf0, unsigned int(__thiscall *)(void *, unsigned int))(*reinterpret_cast<void **>(0x911fa8), 20);
            ++At<int>(self, kCount);
            At<float>(self, kPeriod) = static_cast<float>(extra) + 25.0f;
        }
        else if (++At<int>(self, kCount) > 0x20)
            At<int>(self, kCount) = 0;
    }
    if (At<char>(self, kFading))
    {
        const float left = At<float>(self, kFadeLeft) - dt;
        At<float>(self, kFadeLeft) = left;
        if (left < 0.0f)
            Virtual(self, kVirtualFadeEnd);
    }
}
