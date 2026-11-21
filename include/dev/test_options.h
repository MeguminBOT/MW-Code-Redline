#pragma once

namespace nfs::test
{
/** `-testoptions N`: changes a video option every N seconds through the game's reset path (source/dev/TestOptions.cpp).
 * Called at the start of each frame; does nothing without the switch. */
void CycleVideoOptions();
} // namespace nfs::test
