#pragma once

namespace nfs
{
/** Appends a line to nfs.log next to the exe (wsprintf format: no floats) and sends it to the debugger. */
void Log(const char *fmt, ...);
} // namespace nfs
