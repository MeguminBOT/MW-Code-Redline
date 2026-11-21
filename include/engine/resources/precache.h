#pragma once

/**
 * The precache (source/engine/resources/PackFiles.cpp, docs/PACKS.md "Precache"): files the game is about to read,
 * read ahead on a thread of their own and served from memory, in place of EA's memory files. GLOBAL\PRECACHE.TXT
 * (tools/assetc/precache.py, from assets/global/precache.txt) lists them by phase: "boot" from the moment the packs
 * mount until the global data has loaded, "race" from a race's loading until it has finished loading, "keep" for the
 * whole run. An item is a whole file or the first bytes of one a pack serves (a streamed pack's header).
 */
namespace nfs::precache
{
/** Starts reading a phase's files (the boot and keep phases start by themselves when the packs mount). */
void Begin(const char *phase);
/** Lets a phase's files go (each once nothing has it open any more). */
void Release(const char *phase);
} // namespace nfs::precache
