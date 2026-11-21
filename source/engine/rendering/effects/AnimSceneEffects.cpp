/** Effects of the animated scenes (cutscene characters) (original: zAnim CAnimScene::RenderEffects,
 * RenderAnimSceneEffects). */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
const int kEntities = 0x14;
const int kNodeNext = 4;
const int kNodeEntity = 8;
const int kEntityRenderEffects = 0x1c;
const int kNisScene = 0x4c;
const int kPassedFlags = 0x800;
} // namespace

void CAnimScene::RenderEffects(eView *view, int flags)
{
    char *head = reinterpret_cast<char *>(this) + kEntities;
    for (char *node = *reinterpret_cast<char **>(head + kNodeNext); node != head;
         node = *reinterpret_cast<char **>(node + kNodeNext))
    {
        void *entity = *reinterpret_cast<void **>(node + kNodeEntity);
        using Render = void(__thiscall *)(void *, eView *, int);
        reinterpret_cast<Render>((*reinterpret_cast<void ***>(entity))[kEntityRenderEffects / 4])(entity, view, flags);
    }
}

void RenderAnimSceneEffects(eView *view, int flags)
{
    void *nis = *reinterpret_cast<void **>(0x9885c8);
    if (!nis)
        return;
    using Scene = CAnimScene *(__thiscall *)(void *);
    CAnimScene *scene = reinterpret_cast<Scene>((*reinterpret_cast<void ***>(nis))[kNisScene / 4])(nis);
    if (scene)
        scene->RenderEffects(view, flags & kPassedFlags);
}
