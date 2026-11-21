/**
 * Whether the original game's bugs are fixed (gameplay/vanillafixes.h): the loaded profile's Vanilla Bug Fixes. Not in the
 * original game.
 */
#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "core/log.h"
#include "gameplay/progression/profileoptions.h"
#include "gameplay/vanillafixes.h"

namespace nfs::vanillafixes
{
namespace
{
const int kUnset = -1;

/** The answer for the profile of that name, until the profile options learn something new. */
struct Cache
{
    bool known;
    unsigned int changes;
    char profile[32];
    bool on;
};

Cache g_cache;
} // namespace

bool On()
{
    static const int forced = nfs::config::Int("Debug", "VanillaFixes", kUnset);
    if (forced != kUnset)
        return forced != 0;
    const UserProfile *profile = FEDatabase ? FEDatabase->CurrentUserProfile : nullptr;
    if (!profile)
        return false;
    const unsigned int changes = nfs::profileoptions::Changes();
    if (g_cache.known && g_cache.changes == changes && lstrcmpA(g_cache.profile, profile->m_aProfileName) == 0)
        return g_cache.on;
    g_cache.known = true;
    g_cache.changes = changes;
    lstrcpynA(g_cache.profile, profile->m_aProfileName, sizeof(g_cache.profile));
    g_cache.on = nfs::profileoptions::ForProfile(profile->m_aProfileName).vanillaFixes;
    nfs::Log("vanilla fixes: %s for profile %s", g_cache.on ? "on" : "off",
             profile->m_aProfileName[0] ? profile->m_aProfileName : "(none)");
    return g_cache.on;
}
} // namespace nfs::vanillafixes
