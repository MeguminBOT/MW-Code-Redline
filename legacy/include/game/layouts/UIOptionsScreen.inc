// bases: public UIWidgetMenu
bool mCalledFromPauseMenu;
bool NeedsColorCal;
bool mInitialAutoSaveValue;
class FEToggleWidget *speakeroption;
class FESliderWidget *volumeoption;
class AudioSettings *OriginalAudioSettings;
class VideoSettings *OriginalVideoSettings;
class GameplaySettings *OriginalGameplaySettings;
class PlayerSettings *OriginalPlayerSettings;
class cOnlineSettings *OriginalOnlineSettings;
struct VideoOptions *WorkingVideoOptions;
int WorkingPerformanceLevel;
struct VideoOptions *OriginalVideoOptions;
int OriginalPerformanceLevel;
bool ShowAdvancedVideo;
void Setup();
void NotificationMessage(unsigned int message, class FEObject *object, unsigned int param1, unsigned int param2);
