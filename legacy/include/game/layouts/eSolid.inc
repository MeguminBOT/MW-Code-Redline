/** eSolidPlatInterface: the solid's platform data. */
struct SolidPlatInfo *PlatInfo;
/** bTNode<eSolid>. */
class eSolid *Next;
class eSolid *Prev;
unsigned char Version;
unsigned char EndianSwapped;
unsigned short Flags;
unsigned int NameHash;
short NumPolys;
short NumVerts;
signed char NumBones;
signed char NumTextureTableEntries;
signed char NumLightMaterials;
signed char NumPositionMarkerTableEntries;
int ReferencedFrameCounter;
float AABBMinX;
float AABBMinY;
float AABBMinZ;
/** Name hash and texture pairs. */
struct eTextureEntry *pTextureTable;
float AABBMaxX;
float AABBMaxY;
float AABBMaxZ;
/** Name hash and light material pairs. */
struct eLightMaterialEntry *LightMaterialTable;
