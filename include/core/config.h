#pragma once

/**
 * Settings of the rebuilt game: speed.ini next to the exe (template in rebuild/assets/config/speed.ini). Missing keys and a
 * missing file give the documented defaults. `-set Section.Key=Value` on the command line overrides a key for that run
 * (tests), without writing speed.ini.
 *
 * What belongs to a profile ([Profiles]: its car performance, [Profile NAME]: its options page, [UnlockAll]: the test
 * profiles) is in profiles.ini next to it (the Profile* functions); the first use moves those sections out of speed.ini.
 */
namespace nfs::config
{
int Int(const char *section, const char *key, int fallback);
float Float(const char *section, const char *key, float fallback);
/** Copies the value (or `fallback`) into `out`, lower-cased. */
void String(const char *section, const char *key, const char *fallback, char *out, int size);
/** Stores a value, creating speed.ini if needed (for settings changed in game). */
void WriteString(const char *section, const char *key, const char *value);
/** A float stored with three decimals. */
void WriteFloat(const char *section, const char *key, float value);
void WriteInt(const char *section, const char *key, int value);
/** Whether speed.ini has the key (the command line's -set counts). */
bool Has(const char *section, const char *key);

/** The same for profiles.ini. */
int ProfileInt(const char *section, const char *key, int fallback);
void ProfileString(const char *section, const char *key, const char *fallback, char *out, int size);
void ProfileWriteString(const char *section, const char *key, const char *value);
/** Moves speed.ini's profile sections into profiles.ini now (at boot) rather than at the first profile setting read. */
void MoveProfileSections();
} // namespace nfs::config
