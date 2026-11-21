#pragma once

/** The game's global data at boot (source/engine/resources/GlobalLoading.cpp). */
namespace nfs::globalload
{
/** Loads a resource file and waits for it, as the global data waits for its own files; nothing when it is missing. */
void LoadResourceFile(const char *path);
} // namespace nfs::globalload
