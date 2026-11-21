/**
 * The attribute vaults from our tables (docs/EA_FORMATS.md phase 2 step 2): InitializeSingleAttributeVault (0x664430,
 * original: GameFlow; replaced in gameplay/vehicles/Realism.cpp, which calls InitializeVault here) lays a vault out
 * from GLOBAL\ATTRIBUTES.TABLES (tools/assetc/attribtables.py, laid out by AttribTables.cpp) when the packs have it, and hands the images to the game's own AddDepFile and AddVault as the
 * original does with a vault of its VPAK image: the .bin copied into memory the vault keeps (`allocFlags`), the .vlt
 * only while AddVault reads it. Without the tables (or a vault they lack) the EA image the caller passes is read as
 * before. `-set Data.AttribTables=0` reads the EA images.
 */
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/config.h"
#include "core/log.h"
#include "engine/resources/attribtables.h"
#include "engine/resources/pack.h"

namespace
{
const char kTables[] = "GLOBAL\\ATTRIBUTES.TABLES";

bool Enabled()
{
    static const bool on = nfs::config::Int("Data", "AttribTables", 1) != 0;
    return on;
}
} // namespace

bool nfs::attribtables::Available()
{
    return Enabled() && nfs::pack::Exists(kTables);
}

Attrib::Vault *nfs::attribtables::InitializeVault(void *buffer, const char *name, unsigned char **permanent,
                                                  unsigned int allocFlags)
{
    if (Enabled())
    {
        uint32_t size = 0;
        uint8_t *table = nfs::pack::ReadWhole(kTables, size);
        uint8_t *bin = nullptr, *vlt = nullptr;
        size_t binSize = 0, vltSize = 0;
        const bool laid = table && nfs::attribtables::Layout(table, size, name, bin, binSize, vlt, vltSize);
        ::operator delete(table);
        if (laid)
        {
            unsigned char *kept = static_cast<unsigned char *>(bMalloc(static_cast<int>(binSize), static_cast<int>(allocFlags)));
            memcpy(kept, bin, binSize);
            nfs::attribtables::FreeLayout(bin);
            char text[64];
            bSPrintf(text, "%s.bin", name);
            AddDepFile(text, kept, static_cast<unsigned int>(binSize));
            bSPrintf(text, "%s.vlt", name);
            // The .vlt in the game's own memory, freed after AddVault as the caller frees the VPAK image it read: what
            // Attrib leaves pointing into it keeps its bytes until that memory is used again, as with the original.
            char *image = static_cast<char *>(bMalloc(static_cast<int>(vltSize), 0x1040));
            memcpy(image, vlt, vltSize);
            nfs::attribtables::FreeLayout(vlt);
            Attrib::Vault *vault = AddVault(text, image, static_cast<unsigned int>(vltSize));
            bFree(image);
            if (permanent)
                *permanent = kept;
            nfs::Log("attributes: vault %s from the tables (%u + %u bytes)", name, static_cast<unsigned>(binSize),
                     static_cast<unsigned>(vltSize));
            return vault;
        }
        nfs::Log("attributes: no vault %s in the tables; read from its EA image", name);
    }
    return NFS_ORIGINAL(0x664430, Attrib::Vault *(__cdecl *)(void *, const char *, unsigned char **, unsigned int))(
        buffer, name, permanent, allocFlags);
}
