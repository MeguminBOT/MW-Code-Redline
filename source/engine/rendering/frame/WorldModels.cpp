/**
 * The world models (props placed as models rather than scenery: shop fronts, signs, overhangs, smashed scenery) drawn
 * into a view (original: RenderWorldModels 0x7538d0).
 *
 * The original leaves out a model flagged for it (+0x2a) whose bounding box holds the view's camera, so a model never
 * fills the player's screen from inside. The sun shadow cascades are culled with the player camera's position, so the
 * same test took those models out of the shadows too: driving under or past one (the shop fronts by the start of the
 * debug race), its shadow vanished from the street and the facades the moment the camera entered its box, and came
 * back as the camera left. The cascades now draw every enabled model; other views keep the original.
 */
#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "dev/proving.h"

namespace
{
const int kShadowView = 15;
/** bTList<WorldModel> WorldModelList: its head (next, prev) links the models. */
const unsigned int kWorldModelList = 0x9b0f90;
const int kSplitScreenViewMode = 3;
} // namespace

void __cdecl RenderWorldModels(eView *view, int flags)
{
    nfs::proving::RenderWorldModels(view, flags);
    if (view->ID != kShadowView)
    {
        NFS_ORIGINAL(0x7538d0, void(__cdecl *)(eView *, int))(view, flags);
        return;
    }
    const bool splitScreen = static_cast<int>(eGetCurrentViewMode()) == kSplitScreenViewMode;
    char *const head = reinterpret_cast<char *>(kWorldModelList);
    for (char *model = *reinterpret_cast<char **>(head); model != head; model = *reinterpret_cast<char **>(model))
    {
        // +0x28: drawn at all; +0x29: drawn in split screen.
        if (!model[0x28] || (splitScreen && !model[0x29]))
            continue;
        reinterpret_cast<WorldModel *>(model)->Render(view, flags);
    }
    RenderAnimSceneEffects(view, flags);
}
