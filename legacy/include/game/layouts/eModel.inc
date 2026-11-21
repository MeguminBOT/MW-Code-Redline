/** bTNode<eModel>. */
class eModel *Next;
class eModel *Prev;
unsigned int NameHash;
class eSolid *Solid;
/** Textures this model draws in place of the solid's (car skins, liveries, world model variants). */
class eReplacementTextureTable *pReplacementTextureTable;
int NumReplacementTextures;
