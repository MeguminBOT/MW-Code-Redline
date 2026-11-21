/**
 * The player's preferences in speed.ini (gameplay/progression/preferences.h). Original: zPlatform LoadVideoOptions 0x6c1c00 and
 * SaveVideoOptions 0x6c1f10 (the registry key Software\EA Games\Need for Speed Most Wanted).
 */
#include <stddef.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/progression/preferences.h"
#include "engine/rendering/display/video.h"

NFS_REPLACE(0x6c1c00, LoadVideoOptions)
NFS_REPLACE(0x6c1f10, SaveVideoOptions)

/** The video mode the visual treatment last used (read with the options, not written). */
extern "C" int lpData_00982c78;

namespace
{
const char kVideo[] = "Video";
const char kAudio[] = "Audio";
const char kGameplay[] = "Gameplay";

struct IntField
{
    const char *key;
    size_t offset;
};

/** The video options as the registry had them, under the same names without the "g_". */
const IntField kVideoFields[] = {
    {"Version", offsetof(VideoOptions, Version)},
    {"Size", offsetof(VideoOptions, Size)},
    {"CarEnvironmentMapEnable", offsetof(VideoOptions, CarEnvironmentMapEnable)},
    {"CarEnvironmentMapUpdateData", offsetof(VideoOptions, CarEnvironmentMapUpdateData)},
    {"RoadReflectionEnable", offsetof(VideoOptions, RoadReflectionEnable)},
    {"MotionBlurEnable", offsetof(VideoOptions, MotionBlurEnable)},
    {"ParticleSystemEnable", offsetof(VideoOptions, ParticleSystemEnable)},
    {"WorldLodLevel", offsetof(VideoOptions, WorldLodLevel)},
    {"CarLodLevel", offsetof(VideoOptions, CarLodLevel)},
    {"OverBrightEnable", offsetof(VideoOptions, OverBrightEnable)},
    {"FSAALevel", offsetof(VideoOptions, FSAALevel)},
    {"RainEnable", offsetof(VideoOptions, RainEnable)},
    {"TextureFiltering", offsetof(VideoOptions, TextureFiltering)},
    {"RacingResolution", offsetof(VideoOptions, RacingResolution)},
    {"FirstTime", offsetof(VideoOptions, FirstTime)},
    {"VSyncOn", offsetof(VideoOptions, VSyncOn)},
    {"ShadowDetail", offsetof(VideoOptions, ShadowDetail)},
    {"VisualTreatment", offsetof(VideoOptions, VisualTreatment)},
};

int &VideoField(size_t offset)
{
    return *reinterpret_cast<int *>(reinterpret_cast<char *>(&g_VideoOptions) + offset);
}

void WriteBool(const char *section, const char *key, bool value)
{
    nfs::config::WriteInt(section, key, value ? 1 : 0);
}
} // namespace

void __cdecl SaveVideoOptions()
{
    // A test run's options (-testvideo) are not the player's.
    if (nfs::video::TestOptions())
        return;
    for (const IntField &f : kVideoFields)
        nfs::config::WriteInt(kVideo, f.key, VideoField(f.offset));
    nfs::config::WriteInt(kVideo, "PerformanceLevel", g_PerformanceLevel);
}

void __cdecl LoadVideoOptions()
{
    if (!nfs::prefs::HaveVideo())
    {
        // The first start of this build: the registry's options, moved here.
        NFS_ORIGINAL(0x6c1c00, void(__cdecl *)())();
        SaveVideoOptions();
        nfs::Log("preferences: the registry's video options moved to speed.ini [Video]");
        return;
    }
    for (const IntField &f : kVideoFields)
        VideoField(f.offset) = nfs::config::Int(kVideo, f.key, VideoField(f.offset));
    g_PerformanceLevel = nfs::config::Int(kVideo, "PerformanceLevel", g_PerformanceLevel);
    lpData_00982c78 = nfs::config::Int(kVideo, "VTMode", lpData_00982c78);
}

namespace nfs::prefs
{
bool HaveVideo()
{
    return config::Has(kVideo, "Version");
}

void StoreFromProfile(const UserProfile *profile)
{
    if (!profile)
        return;
    const AudioSettings &a = profile->TheOptionsSettings.TheAudioSettings;
    config::WriteFloat(kAudio, "MasterVolume", a.MasterVol);
    config::WriteFloat(kAudio, "SpeechVolume", a.SpeechVol);
    config::WriteFloat(kAudio, "FrontEndMusicVolume", a.FEMusicVol);
    config::WriteFloat(kAudio, "InGameMusicVolume", a.IGMusicVol);
    config::WriteFloat(kAudio, "SoundEffectsVolume", a.SoundEffectsVol);
    config::WriteFloat(kAudio, "EngineVolume", a.EngineVol);
    config::WriteFloat(kAudio, "CarVolume", a.CarVol);
    config::WriteFloat(kAudio, "AmbientVolume", a.AmbientVol);
    config::WriteFloat(kAudio, "SpeedVolume", a.SpeedVol);
    config::WriteInt(kAudio, "AudioMode", a.AudioMode);
    config::WriteInt(kAudio, "InteractiveMusicMode", a.InteractiveMusicMode);
    config::WriteInt(kAudio, "EATraxMode", a.EATraxMode);
    config::WriteInt(kAudio, "PlayState", a.PlayState);
    const GameplaySettings &g = profile->TheOptionsSettings.TheGameplaySettings;
    WriteBool(kGameplay, "AutoSave", g.AutoSaveOn);
    WriteBool(kGameplay, "RearViewMirror", g.RearviewOn);
    WriteBool(kGameplay, "Damage", g.Damage);
    config::WriteInt(kGameplay, "SpeedoUnits", g.SpeedoUnits);
    config::WriteInt(kGameplay, "RacingMiniMap", g.RacingMiniMapMode);
    config::WriteInt(kGameplay, "ExploringMiniMap", g.ExploringMiniMapMode);
    config::WriteInt(kGameplay, "MapItems", static_cast<int>(g.MapItems));
    WriteBool(kGameplay, "JumpCam", g.JumpCam);
    config::WriteFloat(kGameplay, "HighlightCam", g.HighlightCam);
}

void ApplyToProfile(UserProfile *profile)
{
    if (!profile)
        return;
    if (!config::Has(kAudio, "MasterVolume") || !config::Has(kGameplay, "AutoSave"))
    {
        StoreFromProfile(profile);
        nfs::Log("preferences: %s's audio and gameplay options moved to speed.ini", profile->m_aProfileName);
        return;
    }
    AudioSettings &a = profile->TheOptionsSettings.TheAudioSettings;
    a.MasterVol = config::Float(kAudio, "MasterVolume", a.MasterVol);
    a.SpeechVol = config::Float(kAudio, "SpeechVolume", a.SpeechVol);
    a.FEMusicVol = config::Float(kAudio, "FrontEndMusicVolume", a.FEMusicVol);
    a.IGMusicVol = config::Float(kAudio, "InGameMusicVolume", a.IGMusicVol);
    a.SoundEffectsVol = config::Float(kAudio, "SoundEffectsVolume", a.SoundEffectsVol);
    a.EngineVol = config::Float(kAudio, "EngineVolume", a.EngineVol);
    a.CarVol = config::Float(kAudio, "CarVolume", a.CarVol);
    a.AmbientVol = config::Float(kAudio, "AmbientVolume", a.AmbientVol);
    a.SpeedVol = config::Float(kAudio, "SpeedVolume", a.SpeedVol);
    a.AudioMode = config::Int(kAudio, "AudioMode", a.AudioMode);
    a.InteractiveMusicMode = config::Int(kAudio, "InteractiveMusicMode", a.InteractiveMusicMode);
    a.EATraxMode = config::Int(kAudio, "EATraxMode", a.EATraxMode);
    a.PlayState = config::Int(kAudio, "PlayState", a.PlayState);
    GameplaySettings &g = profile->TheOptionsSettings.TheGameplaySettings;
    g.AutoSaveOn = config::Int(kGameplay, "AutoSave", g.AutoSaveOn) != 0;
    g.RearviewOn = config::Int(kGameplay, "RearViewMirror", g.RearviewOn) != 0;
    g.Damage = config::Int(kGameplay, "Damage", g.Damage) != 0;
    g.SpeedoUnits = static_cast<unsigned char>(config::Int(kGameplay, "SpeedoUnits", g.SpeedoUnits));
    g.RacingMiniMapMode = static_cast<unsigned char>(config::Int(kGameplay, "RacingMiniMap", g.RacingMiniMapMode));
    g.ExploringMiniMapMode =
        static_cast<unsigned char>(config::Int(kGameplay, "ExploringMiniMap", g.ExploringMiniMapMode));
    g.MapItems = static_cast<unsigned int>(config::Int(kGameplay, "MapItems", static_cast<int>(g.MapItems)));
    g.JumpCam = config::Int(kGameplay, "JumpCam", g.JumpCam) != 0;
    g.HighlightCam = config::Float(kGameplay, "HighlightCam", g.HighlightCam);
}
} // namespace nfs::prefs
