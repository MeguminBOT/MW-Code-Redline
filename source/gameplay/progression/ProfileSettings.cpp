/**
 * Profile settings and toggle UI - enables checkbox-style options during profile creation.
 * 
 * This replaces the old yes/no dialog for enabling realism mode with a set of checkboxes/toggles
 * similar to how the Options screen works, allowing users to enable/disable specific features:
 * - Difficulty Level
 * - Realistic Car Performance
 * - Other future features
 */

#include <windows.h>

#include "game/gen/api.h"
#include "gameplay/progression/profilesettings.h"
#include "legacy/link.h"

namespace nfs
{
// Helper implementations for preset profiles
ProfileSettings CreateDefaultProfile(const char* name)
{
    ProfileSettings settings;
    if (name) lstrcpynA(settings.name, name, sizeof(settings.name));
    // Default settings: balanced gameplay
    return settings;
}

ProfileSettings CreateEasyModeProfile(const char* name)
{
    ProfileSettings settings;
    if (name) lstrcpynA(settings.name, name, sizeof(settings.name));
    settings.difficultyEnabled = false;
    settings.realisticCarPerformance = false;
    return settings;
}

ProfileSettings CreateHardcoreProfile(const char* name)
{
    ProfileSettings settings;
    if (name) lstrcpynA(settings.name, name, sizeof(settings.name));
    settings.difficultyEnabled = true;
    settings.realisticCarPerformance = true;
    return settings;
}

ProfileSettings CreateUnlockAllProfile(const char* name)
{
    ProfileSettings settings;
    if (name) lstrcpynA(settings.name, name, sizeof(settings.name));
    settings.difficultyEnabled = true; // For unlock checks
    settings.realisticCarPerformance = false;
    return settings;
}

void ProfileSettings::EnableAllFeatures()
{
    difficultyEnabled = true;
    realisticCarPerformance = true;
    featureNotYetImplemented1 = true;
    featureNotYetImplemented2 = true;
}

void ProfileSettings::DisableAllFeatures()
{
    difficultyEnabled = false;
    realisticCarPerformance = false;
    featureNotYetImplemented1 = false;
    featureNotYetImplemented2 = false;
}

// Global settings instance (used by the frontend)
ProfileSettings TheProfileSettings;

const ProfileSettings& GetProfileSettings() { return TheProfileSettings; }
void SetProfileSettings(const ProfileSettings& settings) { TheProfileSettings = settings; }

} // namespace nfs
