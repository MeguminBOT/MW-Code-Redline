// bases: public TextureInfoPlatInterface
int unknown_04[2];
char Name[0x18];
unsigned int NameHash;
int unknown_28[4];
/** Bytes of all mip levels at one byte per texel (palettized textures). */
int ImageSize;
int unknown_3c;
/** Bytes of the top mip level; each level below is a quarter. */
int BaseImageSize;
short Width;
short Height;
char unknown_48[2];
unsigned char BitsPerPixel;
char unknown_4b[3];
char NumMipMapLevels;
/** Bit 0: repeat in U, bit 1: repeat in V (clamped otherwise). */
unsigned char TilableUV;
char unknown_50[2];
/** 3: scrolls by ScrollSpeed; 0: does not scroll. */
char ScrollType;
char unknown_53;
char unknown_54;
/** 1: alpha tested (cut out). */
char AlphaUsageType;
/** 0 opaque, 1 alpha blend, 2 additive, 3 darken (destination times one minus source), 4 alpha blend, 5 destination
 * alpha, 6 destination alpha additive. */
char AlphaBlendType;
unsigned char Flags57;
char unknown_58[6];
short ScrollSpeedS;
short ScrollSpeedT;
char unknown_62[6];
class TexturePack *Pack;
int unknown_6c;
void *ImageData;
void *PaletteData;
