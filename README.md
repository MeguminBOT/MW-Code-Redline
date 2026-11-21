# NFSMW Code Redline
A work in progress mod of NFS Most wanted.

This project aims to be a remastered experience authentic to the original, we don't have enough good remasters that are accurate to the era the games were made and played out in. 

So here we aim for 2006 and older cars.

Project is currently not compileable by the public.

Development progress can be seen here:
https://youtu.be/N4tTjiCMrSY?list=PLRpJGMcAM2Gs&t=1

## Current feature list:
- Vulkan support.
- Running on SDL3.
- Dynamic Time
  - Smooth Day and Night cycle.
  - Night skybox from NFS Carbon.
- Dynamic Weather
  - Clear, Cloudy, Rain, Storm
  - Dynamic fog and road wetness based on time and weather.
  - Added water puddles and water splash effects to roads.
- Improved car sounds (using their correct engines from various NFS games)
- Added parallax bump mapping to roads.
- Reworked lightning/shadow system
- Reworked reflection system
- Reworked mipmap system
- Reworked graphic settings
- Reworked AI logic for racers and cops with vastly reduced rubberbanding in favor of MUCH better driving using machine learned data.
- Extended camera view settings (FOV, Distance, Height etc.)
- Upscaled Xbox 360 Textures by 2x
- New mod system (compatible with binary and modloader cars)
- New data system
- New loading system
- Optimizations and rewritten parts of the original game engine (Ecstasy)
- Every career gets the $10,000 bonus the original only gave with Underground 2 saves

- Bugfixes
  - Track barrier glitches removed. (Toggleable)
  - Out of bounds glitches removed. (Toggleable)
  - Cops can now bust you on the bus station.
  - Cops can now bust you on the park scene area.
  - Cops no longer randomly disappear.
  - Golf GTI turbo fixed, stock and all upgrades are now using the correct values.

- New profile creation toggles to change up gameplay:
  - Cars from NFS Underground 2, Carbon and Heat within the same era.  
  - Improved AI
  - Difficulty system
  - Realistic Performance mode (Toggleable on profile creation)
  - Speedrunner mode (brings back exploitable bugs from v1.2 and disables our own bugfixes as well)

## Future goals
- Connect Carbon carrer directly to begin after bridge jump. (Re-use Most wanted UI and Menus)
- Free roam to Bayview and Palmont.
- 64-bit builds to enable AMD FSR and Ray tracing support.
- Engine swapping from Heat/Unbound.
- Remake some "3D" assets on the map to be real 3D.
- Add loved tracks from UG1, UG2, Carbon, Hot Pursuit etc. 
- Improve car geometry of Underground 2 cars and parts to match Most Wanteds.
- Native MacOS, Linux and Android builds.
- Port customizations from UG2
- Port Dyno from UG2 (as an option rather than forced)
- Slight rebalance of all cars (as an option).


## Development tools
These are tools that will release together with this mod:
- Blender addon for car and world editing.
- Two MCP servers for AI Coding Agents.
  - NFSGAME: Runs the game and has full access to the game including memory addresses.
  - NFS3D: External MCP that can work on the 3D objects and optionally work together with the blender addon 
