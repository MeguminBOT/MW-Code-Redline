/**
 * Profile option defaults (original: zFe OptionsSettings::OptionsSettings 0x5796a0 and OptionsSettings::Default
 * 0x56d660, with the GameplaySettings, VideoSettings and PlayerSettings defaults inlined).
 * Changes from the original:
 * - VideoSettings::WideScreen (the console "Widescreen" option, which the PC build never exposed) defaults to on when
 *   the monitor is wider than 4:3.
 * - A new profile keeps the audio and gameplay settings of the profile that was loaded (cFrontendDatabase::DefaultProfile
 *   0x5ab9c0 resets the whole profile before it is named), which are speed.ini's (gameplay/progression/preferences.h).
 */
#include "game/gen/api.h"
#include "engine/rendering/display/display.h"
#include "legacy/link.h"
#include "gameplay/progression/preferences.h"

NFS_REPLACE(0x5ab9c0, cFrontendDatabase::DefaultProfile)

extern "C" int CurrentLanguage;

namespace
{
void DefaultVideo(VideoSettings &video)
{
    video.FEScale = 1.0f;
    video.ScreenOffsetX = 0.0f;
    video.ScreenOffsetY = 0.0f;
    video.WideScreen = nfs::display::IsWideMonitor();
}

void DefaultGameplay(GameplaySettings &gameplay)
{
    gameplay.ExploringMiniMapMode = 0;
    gameplay.LastMapView = 0;
    gameplay.MapItems = 0xFFFFFFFF;
    gameplay.HighlightCam = 127.5f;
    gameplay.AutoSaveOn = true;
    gameplay.RearviewOn = true;
    gameplay.Damage = true;
    gameplay.RacingMiniMapMode = 1;
    gameplay.LastMapZoom = 1;
    gameplay.JumpCam = true;
    gameplay.LastPursuitMapZoom = 2;
    gameplay.SpeedoUnits = CurrentLanguage != 0;
}

void DefaultPlayer(PlayerSettings &player)
{
    player.Config = 0;
    player.SplitTimeType = 0;
    player.Transmission = 0;
    player.GaugesOn = true;
    player.PositionOn = true;
    player.LapInfoOn = true;
    player.ScoreOn = true;
    player.LeaderboardOn = true;
    player.TransmissionPromptOn = true;
    player.Rumble = true;
    player.DriveWithAnalog = true;
    player.CurCam = 2;
    player.Handling = 1;
}
} // namespace

OptionsSettings::OptionsSettings()
{
    DefaultVideo(TheVideoSettings);
    DefaultGameplay(TheGameplaySettings);
    TheAudioSettings.Default();
    DefaultPlayer(ThePlayerSettings[0]);
    DefaultPlayer(ThePlayerSettings[1]);
    Default();
}

void OptionsSettings::Default()
{
    CurrentCategory = 0;
    DefaultVideo(TheVideoSettings);
    TheAudioSettings.Default();
    DefaultGameplay(TheGameplaySettings);
    DefaultPlayer(ThePlayerSettings[0]);
    DefaultPlayer(ThePlayerSettings[1]);
}

void cFrontendDatabase::DefaultProfile()
{
    OptionsSettings &options = CurrentUserProfile->TheOptionsSettings;
    const AudioSettings audio = options.TheAudioSettings;
    const GameplaySettings gameplay = options.TheGameplaySettings;
    NFS_ORIGINAL(0x5ab9c0, void(__thiscall *)(cFrontendDatabase *))(this);
    OptionsSettings &fresh = CurrentUserProfile->TheOptionsSettings;
    fresh.TheAudioSettings = audio;
    fresh.TheGameplaySettings = gameplay;
    nfs::prefs::ApplyToProfile(CurrentUserProfile);
}
