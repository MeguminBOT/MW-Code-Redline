#pragma once
#include "game/gen/api.h"

/** 16-bit binary angle: 0x10000 is a full turn, 0x4000 a right angle. */
typedef unsigned short bAngle;
/** 16.16 fixed point. */
typedef int bFix;

extern ASinTableEntry bASinTable[];
extern "C" const unsigned short bFastATanTable[257];
extern "C" const unsigned short bFixATanTableLow[];
extern "C" const unsigned short bFixATanTableHigh[];
extern "C" const short bFixSinTable[];
extern "C" const unsigned int bCrcTable[256];

int bSignedMod(int a, int b);
bAngle bFastATan(float x, float y);
bFix bFixSin(bAngle angle);
