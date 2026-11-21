#pragma once


namespace nfs
{
struct ProfileSettings
{
    /** Profile name: up to 31 characters and the terminating zero (the rebuild links the game's CRT, which has
     * no std::string). */
    char name[32];
    bool difficultyEnabled;     // Difficulty setting toggle
    bool realisticCarPerformance; // Realistic car physics toggle
    bool featureNotYetImplemented1; // Placeholder for future features
    bool featureNotYetImplemented2; // Placeholder for future features
    
    ProfileSettings()
        : name()
        , difficultyEnabled(true)   // Default to enabled states
        , realisticCarPerformance(false) // Off by default (easier gameplay)
        , featureNotYetImplemented1(false)
        , featureNotYetImplemented2(false)
    {
    }
    
    void EnableAllFeatures();
    void DisableAllFeatures();
};

// Factory functions for common profile presets
ProfileSettings CreateDefaultProfile(const char* name = "Player 1");
ProfileSettings CreateEasyModeProfile(const char* name = "Easy Mode");
ProfileSettings CreateHardcoreProfile(const char* name = "Hardcore");
ProfileSettings CreateUnlockAllProfile(const char* name = "Unlocked");

} // namespace nfs
