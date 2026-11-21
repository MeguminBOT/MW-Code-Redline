#pragma once

class ResourceFile;

/** The game's strings from our tables (source/engine/resources/Strings.cpp). */
namespace nfs::strings
{
/** A resource file begins loading: a language file's strings come from its .STRINGS table. */
void Begin(ResourceFile *file, const char *name);
/** A resource file unloads: its strings, if they were the ones in use, go. */
void End(ResourceFile *file);
} // namespace nfs::strings
