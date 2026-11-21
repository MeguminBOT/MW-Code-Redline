/**
 * `-testoptions N` (tests): every N seconds the video options change as if applied on the Video page, without being
 * saved, cycling through the ones the renderer reacts to (visual treatment, overbright, motion blur, shadows, road
 * reflections). Each change goes through the game's reset path (g_VideoOptionsChanged), so device resets and effect
 * reloads get exercised on either renderer.
 */
#include <windows.h>

#include "game/gen/api.h"
#include "core/log.h"
#include "dev/test_options.h"
#include "engine/rendering/vulkan/renderer.h"

namespace nfs::test
{
namespace
{
int Interval()
{
    static int seconds = -1;
    if (seconds >= 0)
        return seconds;
    seconds = 0;
    const char *line = GetCommandLineA();
    for (const char *p = line; *p; ++p)
    {
        const char *name = "-testoptions ";
        int i = 0;
        while (name[i] && p[i] == name[i])
            ++i;
        if (!name[i] && (p == line || p[-1] == ' '))
        {
            seconds = bStrToLong(p + i);
            break;
        }
    }
    return seconds;
}
} // namespace

void CycleVideoOptions()
{
    const int interval = Interval();
    if (interval <= 0)
        return;
    static DWORD last;
    static unsigned step;
    const DWORD now = GetTickCount();
    if (!last)
        last = now;
    if (now - last < static_cast<DWORD>(interval) * 1000)
        return;
    last = now;
    VideoOptions &o = g_VideoOptions;
    switch (step++ % 5)
    {
    case 0:
        o.VisualTreatment = !o.VisualTreatment;
        break;
    case 1:
        o.OverBrightEnable = !o.OverBrightEnable;
        break;
    case 2:
        o.MotionBlurEnable = !o.MotionBlurEnable;
        break;
    case 3:
        o.ShadowDetail = o.ShadowDetail ? 0 : 2;
        break;
    case 4:
        o.RoadReflectionEnable = o.RoadReflectionEnable ? 0 : 3;
        break;
    }
    g_VideoOptionsChanged = true;
    nfs::Log("testoptions: visual treatment %d, overbright %d, motion blur %d, shadows %d, road reflection %d (tick %u)",
             o.VisualTreatment, o.OverBrightEnable, o.MotionBlurEnable, o.ShadowDetail, o.RoadReflectionEnable, now);
}
} // namespace nfs::test
