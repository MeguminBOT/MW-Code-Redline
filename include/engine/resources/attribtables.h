#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Attrib
{
class Vault;
}

/** The attribute vaults laid out from our tables (source/engine/resources/AttribTables.cpp, docs/EA_FORMATS.md). */
namespace nfs::attribtables
{
/** The .bin and .vlt images of the vault `vaultName` in the tables (each freed with FreeLayout); false when the tables
 * have no such vault or cannot be read. */
bool Layout(const uint8_t *table, size_t size, const char *vaultName, uint8_t *&bin, size_t &binSize, uint8_t *&vlt,
            size_t &vltSize);
/** The VPAK image (assetc.vaultwrite's file) of the vaults whose file is `fileName` (GAMEPLAY.BIN), after `lead` zero
 * bytes (room for a header the caller writes); freed with FreeLayout. */
bool LayoutPack(const uint8_t *table, size_t size, const char *fileName, size_t lead, uint8_t *&pack, size_t &packSize);
void FreeLayout(uint8_t *p);
/** Whether the packs have the tables (source/engine/resources/AttribVaults.cpp) and they are on. */
bool Available();
/** InitializeSingleAttributeVault: the vault from the tables, else from the caller's EA image. */
Attrib::Vault *InitializeVault(void *buffer, const char *name, unsigned char **permanent, unsigned int allocFlags);
} // namespace nfs::attribtables
