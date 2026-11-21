#pragma once

/** The road network written to the dev folder's roads.csv (source/dev/RoadDump.cpp), with -set Dev.RoadDump=1. */
namespace nfs::roaddump
{
/** Once a frame: writes the file once the world is loaded. */
void Update();
} // namespace nfs::roaddump
