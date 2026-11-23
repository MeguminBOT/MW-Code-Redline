/**
 * The race HUD's information panel and the leader board's inputs (original: zFe2 RaceInformation::Update 0x57a900;
 * LeaderBoard::SetRacerName 0x568950, SetRacerPercentComplete 0x5689c0, SetRacerNumLapsCompleted 0x58c590).
 *
 * RaceInformation: +0x10 the package, +0x38 the position, +0x3c the laps, +0x40 the racers, +0x44 the lap, +0x48 the
 * lap time in seconds, +0x4c no lap time, +0x50 the share of the race done, +0x54/+0x58 a knockout's racers and
 * position, +0x5c the time string, +0x60 the lap label, +0x64/+0x68 the position and knockout groups, +0x6c..+0x7c
 * the numbers' strings. A knockout (race type 4) shows its own counts; sprints, drags and speed traps (types 0, 4, 5)
 * show the share done instead of the lap.
 * LeaderBoard's ILeaderBoard face (its LeaderBoard 0x30 before it): +0xc the laps, +0x10 the player's racer, +0x14 a
 * split to show, and per racer (0x78 apart) +0x18 the name, +0x28 the share done, +0x2c thirty split times, +0x7c the
 * lap time, +0x88 the laps done.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kShown = 0x1744b3;
const unsigned int kHidden = 0x16a259;
const unsigned int kIn = 0x5079c8f8;
const unsigned int kLapLabel = 0xbf9c;
const unsigned int kDoneLabel = 0x59bb1918;
const unsigned int kNoTime = 0x733c8147;
const int kSprint = 0;
const int kCircuit = 1;
const int kLapKnockout = 3;
const int kKnockout = 4;
const int kSpeedTrap = 5;
const int kRacers = 4;
const int kRacerSize = 0x78;
const float kSecondsToTicks = 4000.0f;

void EnsureScript(FEObject *o, unsigned int script)
{
    if (!FEngIsScriptSet(o, script))
        FEngSetScript(o, script, true);
}

GRaceParameters *CurrentRace()
{
    return At<GRaceParameters *>(GRaceStatus::fObj, 0x1968);
}

int RaceType(GRaceParameters *race)
{
    return static_cast<int>(race->GetRaceType());
}

using ShowLapTime = bool(__thiscall *)(void *, void *);
const ShowLapTime kShowLapTime = reinterpret_cast<ShowLapTime>(0x57acc0);
} // namespace

void __fastcall RaceInformationUpdate(char *r, void *, void *player);
void __fastcall LeaderBoardSetRacerName(char *b, void *, int racer, const char *name);
void __fastcall LeaderBoardSetRacerPercentComplete(char *b, void *, int racer, float done, float time, void *player);
void __fastcall LeaderBoardSetRacerNumLapsCompleted(char *b, void *, int racer, int laps, float time, void *player);
NFS_REPLACE(0x57a900, RaceInformationUpdate)
NFS_REPLACE(0x568950, LeaderBoardSetRacerName)
NFS_REPLACE(0x5689c0, LeaderBoardSetRacerPercentComplete)
NFS_REPLACE(0x58c590, LeaderBoardSetRacerNumLapsCompleted)

void __fastcall RaceInformationUpdate(char *r, void *, void *)
{
    const char *package = At<const char *>(r, 0x10);
    if (GRaceStatus::fObj && CurrentRace() && RaceType(CurrentRace()) == kKnockout)
    {
        EnsureScript(At<FEObject *>(r, 0x64), kHidden);
        EnsureScript(At<FEObject *>(r, 0x68), kIn);
        FEPrintf(package, At<FEObject *>(r, 0x6c), "%d", At<int>(r, 0x54));
        FEPrintf(package, At<FEObject *>(r, 0x70), "%d", At<int>(r, 0x58));
    }
    else
    {
        EnsureScript(At<FEObject *>(r, 0x64), kShown);
        EnsureScript(At<FEObject *>(r, 0x68), kHidden);
        FEPrintf(package, At<FEObject *>(r, 0x74), "%d", At<int>(r, 0x40));
        FEPrintf(package, At<FEObject *>(r, 0x78), "%d", At<int>(r, 0x38));
    }
    GRaceParameters *race = CurrentRace();
    const int type = race ? RaceType(race) : -1;
    if (race && (type == kSprint || type == kKnockout || type == kSpeedTrap))
    {
        FEngSetLanguageHash(At<FEString *>(r, 0x60), kDoneLabel);
        FEPrintf(package, At<FEObject *>(r, 0x7c), "%d %%", static_cast<int>(At<float>(r, 0x50)));
    }
    else
    {
        FEngSetLanguageHash(At<FEString *>(r, 0x60), kLapLabel);
        FEPrintf(package, At<FEObject *>(r, 0x7c), "%d/%d", At<int>(r, 0x44), At<int>(r, 0x3c));
    }
    if (At<bool>(r, 0x4c))
    {
        FEngSetLanguageHash(At<FEString *>(r, 0x5c), kNoTime);
        return;
    }
    Timer time;
    time.PackedTime = static_cast<int>(static_cast<double>(At<float>(r, 0x48)) * kSecondsToTicks + 0.5);
    char text[0x10];
    time.PrintToString(text, 4);
    FEPrintf(At<FEString *>(r, 0x5c), "%s", text);
}

void __fastcall LeaderBoardSetRacerName(char *b, void *, int racer, const char *name)
{
    if (racer < kRacers)
        bStrCpy(b + racer * kRacerSize + 0x18, name);
}

void __fastcall LeaderBoardSetRacerPercentComplete(char *b, void *, int racer, float done, float time, void *)
{
    if (racer >= kRacers)
        return;
    float &shown = At<float>(b, racer * kRacerSize + 0x28);
    if (done > 0.0f && done < 100.0f)
    {
        const double laps = static_cast<double>(At<int>(b, 0xc));
        const int now = static_cast<int>(static_cast<double>(done) * laps);
        const int before = static_cast<int>(laps * static_cast<double>(shown));
        GRaceParameters *race = now > before ? CurrentRace() : nullptr;
        if (race)
        {
            int step = 0;
            switch (RaceType(race))
            {
            case kSprint:
            case kKnockout:
                step = 25;
                break;
            case kCircuit:
            case kLapKnockout:
                step = 50;
                break;
            }
            if (step && now % step == 0)
            {
                float &split = At<float>(b, 0x2c + (racer * 30 + now / step) * 4);
                if (split == 0.0f)
                {
                    split = time;
                    const int player = At<int>(b, 0x10);
                    if (player ? racer == player : racer == 1)
                        At<bool>(b, 0x14) = true;
                }
            }
        }
    }
    shown = done;
}

void __fastcall LeaderBoardSetRacerNumLapsCompleted(char *b, void *, int racer, int laps, float time, void *player)
{
    if (racer >= kRacers)
        return;
    int &done = At<int>(b, racer * kRacerSize + 0x88);
    if (laps > 0 && laps < At<int>(b, 0xc) && laps > done && racer == At<int>(b, 0x10))
        kShowLapTime(b - 0x30, player);
    done = laps;
    At<float>(b, racer * kRacerSize + 0x7c) = time;
}
