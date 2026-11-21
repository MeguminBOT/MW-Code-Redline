/** The race stats panel's lines (original: zFe2 InfoStat, TimerStat, RaceResultStat, LapStat, StageStat, SpeedStat,
 * TollboothStat ::Draw). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x594f70, InfoStatDraw)
NFS_REPLACE(0x595170, TimerStatDraw)
NFS_REPLACE(0x5953f0, RaceResultStatDraw)
NFS_REPLACE(0x595560, LapStatDraw)
NFS_REPLACE(0x5956f0, StageStatDraw)
NFS_REPLACE(0x595800, SpeedStatDraw)
NFS_REPLACE(0x595930, TollboothStatDraw)

namespace
{
struct Stat
{
    unsigned char base[0x2c];
    FEString *name;
    FEString *value;
    unsigned char pad34[0x4c - 0x34];
    FEString *extra;
    unsigned int a;
    int count;
    Timer time;
    int number;
};
static_assert(sizeof(Stat) == 0x60, "HUD stat");

const unsigned int kNoValue = 0x5d82dba2;
const unsigned int kNoTime = 0x0fc1bf40;
const unsigned int kResultFlag1e = 0x01e66364;
const unsigned int kResultFlag1d = 0xb7b75185;
const unsigned int kResultFlag1c = 0x5d82dba2;
const float kSecondsToTicks = 4000.0f;
const int kProfileMetric = 0x3b;

Stat *Self(void *stat)
{
    return static_cast<Stat *>(stat);
}

void SetBoth(Stat *s, unsigned int hash)
{
    FEngSetLanguageHash(s->extra, hash);
    FEngSetLanguageHash(s->value, hash);
}

void PrintTime(FEString *string, Timer time, const char *format)
{
    char text[0x20];
    time.PrintToString(text, 0);
    FEPrintf(string, format, text);
}

bool NoTime(const Stat *s)
{
    return s->time.PackedTime == 0;
}

bool SpaceBeforePercent()
{
    switch (GetCurrentLanguage())
    {
    case 1:
    case 2:
    case 6:
    case 7:
    case 13:
        return true;
    default:
        return false;
    }
}

void DrawLapLike(Stat *s, const char *timeFormat)
{
    FEPrintf(s->name, "%d", s->count);
    if (s->number == -1)
    {
        SetBoth(s, kNoValue);
        return;
    }
    if (NoTime(s))
    {
        SetBoth(s, kNoTime);
        return;
    }
    FEPrintf(s->extra, "%d", s->number);
    PrintTime(s->value, s->time, timeFormat);
}
} // namespace

void __fastcall InfoStatDraw(void *stat)
{
    Stat *s = Self(stat);
    FEngSetLanguageHash(s->name, reinterpret_cast<unsigned int &>(s->extra));
    FEngSetLanguageHash(s->value, s->a);
}

void __fastcall TimerStatDraw(void *stat)
{
    Stat *s = Self(stat);
    FEngSetLanguageHash(s->name, s->a);
    PrintTime(s->value, *reinterpret_cast<Timer *>(&s->extra), "%s");
}

void __fastcall RaceResultStatDraw(void *stat)
{
    Stat *s = Self(stat);
    const char *racer = reinterpret_cast<const char *>(s->a);
    FEPrintf(s->name, "%s", *reinterpret_cast<const char *const *>(racer + 8));
    if (racer[0x1e])
        FEngSetLanguageHash(s->value, kResultFlag1e);
    else if (racer[0x1d])
        FEngSetLanguageHash(s->value, kResultFlag1d);
    else if (racer[0x1c])
        FEngSetLanguageHash(s->value, kResultFlag1c);
    else if (!racer[0x21])
        FEngSetLanguageHash(s->value, kNoTime);
    else
    {
        const float seconds = reinterpret_cast<const GTimer *>(racer + 0x14c)->GetTime();
        Timer time;
        time.PackedTime = static_cast<int>(seconds * kSecondsToTicks + 0.5f);
        PrintTime(s->value, time, "%s");
    }
    FEPrintf(s->extra, "%d", *reinterpret_cast<const int *>(racer + 0x10));
}

void __fastcall LapStatDraw(void *stat)
{
    DrawLapLike(Self(stat), "%s");
}

void __fastcall TollboothStatDraw(void *stat)
{
    DrawLapLike(Self(stat), "+%s");
}

void __fastcall StageStatDraw(void *stat)
{
    Stat *s = Self(stat);
    FEPrintf(s->name, "%d%s", (s->count + 1) * 20, SpaceBeforePercent() ? " %" : "%");
    if (NoTime(s))
    {
        SetBoth(s, kNoTime);
        return;
    }
    FEPrintf(s->extra, "%d", s->number);
    PrintTime(s->value, s->time, "%s");
}

void __fastcall SpeedStatDraw(void *stat)
{
    Stat *s = Self(stat);
    FEPrintf(s->name, "%d", s->count);
    const float speed = reinterpret_cast<const float &>(s->time);
    if (speed == 0.0f)
    {
        SetBoth(s, kNoTime);
        return;
    }
    const bool metric = reinterpret_cast<const char *>(FEDatabase->CurrentUserProfile)[kProfileMetric] == 1;
    const float shown = speed * (metric ? 3.6f : 2.23699f);
    FEPrintf(s->extra, "%d", s->number);
    FEPrintf(s->value, "%0.0f", static_cast<double>(shown));
}
