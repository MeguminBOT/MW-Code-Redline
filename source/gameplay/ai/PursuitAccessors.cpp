/** Field accessors, attribute lookups, message kinds, counters and this-adjusting deleting destructors of the cop pursuit classes (original: zAI AIPursuit, AICopManager, AIRoadBlock, AIVehiclePursuit, AIVehicleHelicopter, AIPerpVehicle, AIVehicleCopCar, pursuit formations, pursuit messages, Attrib::Gen pursuitlevels and pursuitsupport; written by tools/porting/trivial_ports.py). */
#include "game/gen/api.h"
#include "legacy/link.h"

void __fastcall AIPursuitDestruct(char *self);
void __fastcall AIVehicleHelicopterDestruct(char *self);
void __fastcall AIVehiclePursuitDestruct(char *self);

NFS_REPLACE(0x402f30, AttribGenpursuitlevelsCollapseInnerRadius)
NFS_REPLACE(0x402f50, AttribGenpursuitlevelsCollapseOuterRadius)
NFS_REPLACE(0x402f90, AttribGenpursuitlevelsCopFormations)
NFS_REPLACE(0x403120, AttribGenpursuitlevelsMaxCopsCollapsing)
NFS_REPLACE(0x403240, AttribGenpursuitlevelsScaleEscalationPerBucket)
NFS_REPLACE(0x403270, AttribGenpursuitlevelsSearchModeHeliSpawnChance)
NFS_REPLACE(0x403340, AttribGenpursuitlevelsStaggerFormationTime)
NFS_REPLACE(0x403370, AttribGenpursuitlevelsTimeBetweenFirstFourSpawn)
NFS_REPLACE(0x4035e0, AttribGenpursuitsupportHeavySupportOptions)
NFS_REPLACE(0x403660, AttribGenpursuitsupportLeaderSupportOptions)
NFS_REPLACE(0x4036e0, AttribGenpursuitsupportMinimumSupportDelay)
NFS_REPLACE(0x403930, IPursuitAIDelete)
NFS_REPLACE(0x4040b0, IAIHelicopterDelete)
NFS_REPLACE(0x405290, ICheaterDelete)
NFS_REPLACE(0x405600, MUnspawnCopGetKind)
NFS_REPLACE(0x405750, MSetCopsEnabledGetKind)
NFS_REPLACE(0x4057f0, MForcePursuitStartGetKind)
NFS_REPLACE(0x4059c0, MReqRoadBlockGetKind)
NFS_REPLACE(0x4062f0, AICopManagerMessageSetAutoSpawnMode)
NFS_REPLACE(0x406540, AICopManagerNoNewPursuitsOrCops)
NFS_REPLACE(0x406550, AICopManagerOnDebugDraw)
NFS_REPLACE(0x4073e0, AIActionStaticRoadBlockBeginAction)
NFS_REPLACE(0x4073f0, AIActionStaticRoadBlockFinishAction)
NFS_REPLACE(0x407450, AIActionStaticRoadBlockOnDebugDraw)
NFS_REPLACE(0x4080d0, MPerpBustedGetKind)
NFS_REPLACE(0x409040, AIPerpVehicleSetCostToState)
NFS_REPLACE(0x409050, AIPerpVehicleGetCostToState)
NFS_REPLACE(0x409400, AIPerpVehicleGetHeat)
NFS_REPLACE(0x409570, AIPerpVehicleOnClearCausality)
NFS_REPLACE(0x409580, AIPerpVehicleGetLastTrafficHitTime)
NFS_REPLACE(0x409850, AIVehiclePursuitSetSupportGoal)
NFS_REPLACE(0x4098d0, HeliVehicleActive)
NFS_REPLACE(0x40a740, MNotifyPursuitLengthGetKind)
NFS_REPLACE(0x40aa70, AIPursuitGetFormationType)
NFS_REPLACE(0x40ae70, AIPursuitGetTarget)
NFS_REPLACE(0x40af30, AIPursuitIsAttemptingRoadBlock)
NFS_REPLACE(0x40afb0, AIPursuitOnDebugDraw)
NFS_REPLACE(0x4107e0, ICopMgrDelete)
NFS_REPLACE(0x411060, AICopManagerIsCopSpawnPending)
NFS_REPLACE(0x420330, AIActionPursuitOffRoadDeleteFrom8)
NFS_REPLACE(0x421000, AIActionHeliPursuitDeleteFrom8)
NFS_REPLACE(0x421290, AIActionHeliExitDeleteFrom8)
NFS_REPLACE(0x421740, AIActionRamDeleteFrom8)
NFS_REPLACE(0x421f40, AIActionStaticRoadBlockIsFinished)
NFS_REPLACE(0x421f50, AIActionStaticRoadBlockDeleteFrom8)
NFS_REPLACE(0x42e240, IPursuitDelete)
NFS_REPLACE(0x42e2f0, IRoadBlockDelete)
NFS_REPLACE(0x430d10, AICopManagerGetCacheName)
NFS_REPLACE(0x430d20, AICopManagerGetLockoutTimeRemaining)
NFS_REPLACE(0x430d30, AICopManagerDeleteFrom8)
NFS_REPLACE(0x430d40, AICopManagerDeleteFrom2c)
NFS_REPLACE(0x430d50, AICopManagerDeleteFrom38)
NFS_REPLACE(0x430d60, AICopManagerDeleteFrom48)
NFS_REPLACE(0x430d70, AICopManagerDeleteFrom54)
NFS_REPLACE(0x430d80, AICopManagerDeleteFrom5c)
NFS_REPLACE(0x4322c0, AIVehiclePursuitSetInPursuit)
NFS_REPLACE(0x4322d0, AIVehiclePursuitGetInPursuit)
NFS_REPLACE(0x4322e0, AIVehiclePursuitSetInFormation)
NFS_REPLACE(0x4322f0, AIVehiclePursuitGetInFormation)
NFS_REPLACE(0x432300, AIVehiclePursuitSetInPosition)
NFS_REPLACE(0x432310, AIVehiclePursuitGetInPosition)
NFS_REPLACE(0x432340, AIVehiclePursuitGetPursuitOffset)
NFS_REPLACE(0x432370, AIVehiclePursuitGetInPositionOffset)
NFS_REPLACE(0x432380, AIVehiclePursuitSetInPositionGoal)
NFS_REPLACE(0x432390, AIVehiclePursuitGetInPositionGoal)
NFS_REPLACE(0x4323a0, AIVehiclePursuitSetBreaker)
NFS_REPLACE(0x4323b0, AIVehiclePursuitGetBreaker)
NFS_REPLACE(0x4323c0, AIVehiclePursuitSetChicken)
NFS_REPLACE(0x4323d0, AIVehiclePursuitGetChicken)
NFS_REPLACE(0x4323e0, AIVehiclePursuitSetDamagedByPerp)
NFS_REPLACE(0x4323f0, AIVehiclePursuitGetDamagedByPerp)
NFS_REPLACE(0x432400, AIVehiclePursuitGetSirenState)
NFS_REPLACE(0x432410, AIVehiclePursuitGetTimeSinceTargetSeen)
NFS_REPLACE(0x432420, AIVehiclePursuitZeroTimeSinceTargetSeen)
NFS_REPLACE(0x432430, AIVehiclePursuitGetSupportGoal)
NFS_REPLACE(0x432440, AIVehiclePursuitSetWithinEngagementRadius)
NFS_REPLACE(0x432450, AIVehiclePursuitWasWithinEngagementRadius)
NFS_REPLACE(0x432460, AIVehiclePursuitDeleteFrom8)
NFS_REPLACE(0x432470, AIVehiclePursuitDeleteFrom4c)
NFS_REPLACE(0x432480, AIVehiclePursuitDeleteFrom54)
NFS_REPLACE(0x432490, AIVehiclePursuitDeleteFrom758)
NFS_REPLACE(0x432530, AIVehicleHelicopterGetDesiredHeightOverDest)
NFS_REPLACE(0x432540, AIVehicleHelicopterSetDesiredHeightOverDest)
NFS_REPLACE(0x432590, AIVehicleHelicopterStrafeToDestIsSet)
NFS_REPLACE(0x4325a0, AIVehicleHelicopterSetStrafeToDest)
NFS_REPLACE(0x4325b0, AIVehicleHelicopterGetHeliSheetCoord)
NFS_REPLACE(0x4325c0, AIVehicleHelicopterGetFuelTimeRemaining)
NFS_REPLACE(0x4325f0, AIVehicleHelicopterSetDustStormIntensity)
NFS_REPLACE(0x432600, AIVehicleHelicopterGetDustStormIntensity)
NFS_REPLACE(0x432610, AIVehicleHelicopterDeleteFrom8)
NFS_REPLACE(0x432620, AIVehicleHelicopterDeleteFrom4c)
NFS_REPLACE(0x432630, AIVehicleHelicopterDeleteFrom54)
NFS_REPLACE(0x432640, AIVehicleHelicopterDeleteFrom758)
NFS_REPLACE(0x432650, AIVehicleHelicopterDeleteFrom7a4)
NFS_REPLACE(0x432960, AIRoadBlockGetPursuit)
NFS_REPLACE(0x432970, AIRoadBlockSetDodged)
NFS_REPLACE(0x432980, AIRoadBlockGetDodged)
NFS_REPLACE(0x432990, AIRoadBlockIncNumCopsDestroyed)
NFS_REPLACE(0x4329a0, AIRoadBlockGetNumCopsDestroyed)
NFS_REPLACE(0x4329b0, AIRoadBlockIncNumCopsDamaged)
NFS_REPLACE(0x4329c0, AIRoadBlockGetNumCopsDamaged)
NFS_REPLACE(0x4329d0, AIRoadBlockGetRoadBlockCentre)
NFS_REPLACE(0x4329e0, AIRoadBlockGetRoadBlockDir)
NFS_REPLACE(0x432a30, AIRoadBlockGetNumSpikeStrips)
NFS_REPLACE(0x432a40, AIRoadBlockGetVehicles)
NFS_REPLACE(0x432a50, AIRoadBlockGetSmackables)
NFS_REPLACE(0x432a60, AIRoadBlockIsPerpCheating)
NFS_REPLACE(0x432a70, AIRoadBlockDeleteFrom8)
NFS_REPLACE(0x432a80, AIRoadBlockDeleteFrom2c)
NFS_REPLACE(0x432a90, AIRoadBlockDeleteFrom38)
NFS_REPLACE(0x432aa0, AIRoadBlockDeleteFrom48)
NFS_REPLACE(0x433170, AICopManagerDelete)
NFS_REPLACE(0x433360, AIPerpVehicleIsHiddenFromCars)
NFS_REPLACE(0x433370, AIPerpVehicleIsHiddenFromHelicopters)
NFS_REPLACE(0x433380, AIPerpVehicleGetPendingRepPointsNormal)
NFS_REPLACE(0x433390, AIPerpVehicleGetPendingRepPointsFromCopDestruction)
NFS_REPLACE(0x4333a0, AIPerpVehicleClearPendingRepPoints)
NFS_REPLACE(0x4333b0, AIPerpVehicleGetPursuitEscalationAttrib)
NFS_REPLACE(0x4333c0, AIPerpVehicleGetPursuitLevelAttrib)
NFS_REPLACE(0x4333d0, AIPerpVehicleGetPursuitSupportAttrib)
NFS_REPLACE(0x4333e0, AIPerpVehicleGetRacerInfo)
NFS_REPLACE(0x433440, AIPerpVehicleGet911CallTime)
NFS_REPLACE(0x433450, AIPerpVehicleDeleteFrom8)
NFS_REPLACE(0x433460, AIPerpVehicleDeleteFrom4c)
NFS_REPLACE(0x433470, AIPerpVehicleDeleteFrom54)
NFS_REPLACE(0x433480, AIPerpVehicleDeleteFrom758)
NFS_REPLACE(0x433490, AIPerpVehicleDeleteFrom760)
NFS_REPLACE(0x4334a0, AIPerpVehicleDeleteFrom76c)
NFS_REPLACE(0x4334e0, AIVehicleCopCarGetSkill)
NFS_REPLACE(0x4334f0, AIVehicleCopCarGetShortcutSkill)
NFS_REPLACE(0x433500, AIVehicleCopCarDeleteFrom8)
NFS_REPLACE(0x433510, AIVehicleCopCarDeleteFrom4c)
NFS_REPLACE(0x433520, AIVehicleCopCarDeleteFrom54)
NFS_REPLACE(0x433530, AIVehicleCopCarDeleteFrom758)
NFS_REPLACE(0x4336b0, AIVehiclePursuitDelete)
NFS_REPLACE(0x4336e0, AIVehicleHelicopterDelete)
NFS_REPLACE(0x433890, AIPursuitIsPerpBusted)
NFS_REPLACE(0x4338c0, AIPursuitGetPursuitDuration)
NFS_REPLACE(0x4338d0, AIPursuitGetEvadeLevel)
NFS_REPLACE(0x4338e0, AIPursuitGetCoolDownTimeRemaining)
NFS_REPLACE(0x4338f0, AIPursuitGetCoolDownTimeRequired)
NFS_REPLACE(0x433900, AIPursuitGetBackupETA)
NFS_REPLACE(0x433910, AIPursuitIsPerpInSight)
NFS_REPLACE(0x433920, AIPursuitIsPursuitBailed)
NFS_REPLACE(0x433930, AIPursuitIsCollapseActive)
NFS_REPLACE(0x433970, AIPursuitGetLastKnownLocation)
NFS_REPLACE(0x433980, AIPursuitGetRoadBlock)
NFS_REPLACE(0x4339b0, AIPursuitGetNumCopsDestroyed)
NFS_REPLACE(0x4339c0, AIPursuitGetNumCopsDamaged)
NFS_REPLACE(0x4339d0, AIPursuitGetTotalNumCopsInvolved)
NFS_REPLACE(0x4339e0, AIPursuitGetNumCopsFullyEngaged)
NFS_REPLACE(0x4339f0, AIPursuitNotifyRoadblockDodged)
NFS_REPLACE(0x433a00, AIPursuitNotifyRoadblockDeployed)
NFS_REPLACE(0x433a40, AIPursuitNotifyTrafficCarHit)
NFS_REPLACE(0x433a70, AIPursuitNotifySpikeStripDeployed)
NFS_REPLACE(0x433aa0, AIPursuitNotifyCopCarDeployed)
NFS_REPLACE(0x433ab0, AIPursuitNotifySupportVehicleDeployed)
NFS_REPLACE(0x433ac0, AIPursuitGetNumRoadblocksDodged)
NFS_REPLACE(0x433ad0, AIPursuitGetNumRoadblocksDeployed)
NFS_REPLACE(0x433ae0, AIPursuitGetValueOfPropertyDamaged)
NFS_REPLACE(0x433af0, AIPursuitGetNumPropertyDamaged)
NFS_REPLACE(0x433b00, AIPursuitGetNumTrafficCarsHit)
NFS_REPLACE(0x433b10, AIPursuitGetNumSpikeStripsDodged)
NFS_REPLACE(0x433b20, AIPursuitGetNumSpikeStripsDeployed)
NFS_REPLACE(0x433b30, AIPursuitGetNumHeliSpikeStripDeployed)
NFS_REPLACE(0x433b40, AIPursuitGetNumCopCarsDeployed)
NFS_REPLACE(0x433b50, AIPursuitGetNumSupportVehiclesDeployed)
NFS_REPLACE(0x433b60, AIPursuitGetPursuitStatus)
NFS_REPLACE(0x433b70, AIPursuitGetTimeToBackupSpawned)
NFS_REPLACE(0x433b80, AIPursuitPendingRoadBlockRequest)
NFS_REPLACE(0x433b90, AIPursuitGetNumHeliSpawns)
NFS_REPLACE(0x433ba0, AIPursuitGetCopDestroyedBonusMultiplier)
NFS_REPLACE(0x433bb0, AIPursuitGetMostRecentCopDestroyedRepPoints)
NFS_REPLACE(0x433bc0, AIPursuitGetMostRecentCopDestroyedType)
NFS_REPLACE(0x433bd0, AIPursuitGetNumCopsInWave)
NFS_REPLACE(0x433c10, AIPursuitGetIsAJerk)
NFS_REPLACE(0x433c20, AIPursuitGetMinDistanceToTarget)
NFS_REPLACE(0x433c30, AIPursuitSetBustedTimerToZero)
NFS_REPLACE(0x433c40, AIPursuitGetEnterSafehouseOnDone)
NFS_REPLACE(0x433c50, AIPursuitDeleteFrom8)
NFS_REPLACE(0x433c60, AIPursuitDeleteFrom2c)
NFS_REPLACE(0x433c70, AIPursuitDeleteFrom38)
NFS_REPLACE(0x433c80, AIPursuitDeleteFrom48)
NFS_REPLACE(0x433c90, AIPursuitGetNumCops)
NFS_REPLACE(0x433cc0, AIRoadBlockDelete)
NFS_REPLACE(0x435260, AIPerpVehicleDelete)
NFS_REPLACE(0x435290, AIVehicleCopCarDelete)
NFS_REPLACE(0x4354d0, AIPursuitDelete)
NFS_REPLACE(0x4380f0, AIGoalPursuitDelete)
NFS_REPLACE(0x438120, AIGoalPullOverDelete)
NFS_REPLACE(0x438230, PursuitFormationUpdate)
NFS_REPLACE(0x438240, PursuitFormationGetFinisherTolerance)
NFS_REPLACE(0x438250, PursuitFormationGetFinisherTime)
NFS_REPLACE(0x438260, PursuitFormationGetTimeToFinisher)
NFS_REPLACE(0x43c230, PursuitFormationDelete)
NFS_REPLACE(0x43f4d0, BoxInFormationGetFinisherTime)
NFS_REPLACE(0x43f4e0, BoxInFormationDelete)
NFS_REPLACE(0x43f720, RollingBlockFormationGetFinisherTime)
NFS_REPLACE(0x43f730, RollingBlockFormationDelete)
NFS_REPLACE(0x43fac0, FollowFormationDelete)
NFS_REPLACE(0x43fe50, StaggerFollowFormationDelete)
NFS_REPLACE(0x43fff0, PitFormationGetTimeToFinisher)
NFS_REPLACE(0x440000, PitFormationGetFinisherTolerance)
NFS_REPLACE(0x440010, PitFormationDelete)
NFS_REPLACE(0x440220, HerdFormationDelete)
NFS_REPLACE(0x565e00, AttribGenpursuitlevelsEvadeSuccessHeatAdjust)
NFS_REPLACE(0x565e10, AttribGenpursuitlevelsEventWinHeatAdjust)
NFS_REPLACE(0x6a4c20, SpikeStripReset)
NFS_REPLACE(0x6fea00, AttribGenpursuitlevelsCTSFor911)
NFS_REPLACE(0x6fea20, AttribGenpursuitlevelsLifetime911)
NFS_REPLACE(0x6fea40, AttribGenpursuitlevelsNumCiviHitsFor911)

const void *__fastcall AttribGenpursuitlevelsCollapseInnerRadius(void *self);
const void *__fastcall AttribGenpursuitlevelsCollapseOuterRadius(void *self);
const void *__fastcall AttribGenpursuitlevelsCopFormations(void *self, void *, unsigned int index);
const void *__fastcall AttribGenpursuitlevelsMaxCopsCollapsing(void *self);
const void *__fastcall AttribGenpursuitlevelsScaleEscalationPerBucket(void *self, void *, unsigned int index);
const void *__fastcall AttribGenpursuitlevelsSearchModeHeliSpawnChance(void *self);
const void *__fastcall AttribGenpursuitlevelsStaggerFormationTime(void *self);
const void *__fastcall AttribGenpursuitlevelsTimeBetweenFirstFourSpawn(void *self);
const void *__fastcall AttribGenpursuitsupportHeavySupportOptions(void *self, void *, unsigned int index);
const void *__fastcall AttribGenpursuitsupportLeaderSupportOptions(void *self, void *, unsigned int index);
const void *__fastcall AttribGenpursuitsupportMinimumSupportDelay(void *self);
void *__fastcall IPursuitAIDelete(char *self, void *, unsigned int flags);
void *__fastcall IAIHelicopterDelete(char *self, void *, unsigned int flags);
void *__fastcall ICheaterDelete(char *self, void *, unsigned int flags);
unsigned int *__cdecl MUnspawnCopGetKind(unsigned int *kind);
unsigned int *__cdecl MSetCopsEnabledGetKind(unsigned int *kind);
unsigned int *__cdecl MForcePursuitStartGetKind(unsigned int *kind);
unsigned int *__cdecl MReqRoadBlockGetKind(unsigned int *kind);
void __fastcall AICopManagerMessageSetAutoSpawnMode(char *, void *, unsigned int);
void __fastcall AICopManagerNoNewPursuitsOrCops(char *self);
void __fastcall AICopManagerOnDebugDraw(char *, void *);
void __fastcall AIActionStaticRoadBlockBeginAction(char *, void *, unsigned int);
void __fastcall AIActionStaticRoadBlockFinishAction(char *, void *, unsigned int);
void __fastcall AIActionStaticRoadBlockOnDebugDraw(char *, void *);
unsigned int *__cdecl MPerpBustedGetKind(unsigned int *kind);
void __fastcall AIPerpVehicleSetCostToState(char *self, void *, unsigned int value);
unsigned int __fastcall AIPerpVehicleGetCostToState(char *self);
float __fastcall AIPerpVehicleGetHeat(char *self);
bool __fastcall AIPerpVehicleOnClearCausality(char *, void *, unsigned int);
float __fastcall AIPerpVehicleGetLastTrafficHitTime(char *self);
void __fastcall AIVehiclePursuitSetSupportGoal(char *self, void *, unsigned int value);
bool __cdecl HeliVehicleActive();
unsigned int *__cdecl MNotifyPursuitLengthGetKind(unsigned int *kind);
unsigned int __fastcall AIPursuitGetFormationType(char *self);
unsigned int __fastcall AIPursuitGetTarget(char *self);
bool __fastcall AIPursuitIsAttemptingRoadBlock(char *self);
void __fastcall AIPursuitOnDebugDraw(char *, void *);
void *__fastcall ICopMgrDelete(char *self, void *, unsigned int flags);
bool __fastcall AICopManagerIsCopSpawnPending(char *self);
void *__fastcall AIActionPursuitOffRoadDeleteFrom8(char *self, void *, unsigned int flags);
void *__fastcall AIActionHeliPursuitDeleteFrom8(char *self, void *, unsigned int flags);
void *__fastcall AIActionHeliExitDeleteFrom8(char *self, void *, unsigned int flags);
void *__fastcall AIActionRamDeleteFrom8(char *self, void *, unsigned int flags);
bool __fastcall AIActionStaticRoadBlockIsFinished(char *, void *);
void *__fastcall AIActionStaticRoadBlockDeleteFrom8(char *self, void *, unsigned int flags);
void *__fastcall IPursuitDelete(char *self, void *, unsigned int flags);
void *__fastcall IRoadBlockDelete(char *self, void *, unsigned int flags);
unsigned int __fastcall AICopManagerGetCacheName(char *);
float __fastcall AICopManagerGetLockoutTimeRemaining(char *self);
void *__fastcall AICopManagerDeleteFrom8(char *self, void *, unsigned int flags);
void *__fastcall AICopManagerDeleteFrom2c(char *self, void *, unsigned int flags);
void *__fastcall AICopManagerDeleteFrom38(char *self, void *, unsigned int flags);
void *__fastcall AICopManagerDeleteFrom48(char *self, void *, unsigned int flags);
void *__fastcall AICopManagerDeleteFrom54(char *self, void *, unsigned int flags);
void *__fastcall AICopManagerDeleteFrom5c(char *self, void *, unsigned int flags);
void __fastcall AIVehiclePursuitSetInPursuit(char *self, void *, unsigned char value);
unsigned char __fastcall AIVehiclePursuitGetInPursuit(char *self);
void __fastcall AIVehiclePursuitSetInFormation(char *self, void *, unsigned char value);
unsigned char __fastcall AIVehiclePursuitGetInFormation(char *self);
void __fastcall AIVehiclePursuitSetInPosition(char *self, void *, unsigned char value);
unsigned char __fastcall AIVehiclePursuitGetInPosition(char *self);
char *__fastcall AIVehiclePursuitGetPursuitOffset(char *self);
char *__fastcall AIVehiclePursuitGetInPositionOffset(char *self);
void __fastcall AIVehiclePursuitSetInPositionGoal(char *self, void *, const unsigned int *value);
char *__fastcall AIVehiclePursuitGetInPositionGoal(char *self);
void __fastcall AIVehiclePursuitSetBreaker(char *self, void *, unsigned char value);
unsigned char __fastcall AIVehiclePursuitGetBreaker(char *self);
void __fastcall AIVehiclePursuitSetChicken(char *self, void *, unsigned char value);
unsigned char __fastcall AIVehiclePursuitGetChicken(char *self);
void __fastcall AIVehiclePursuitSetDamagedByPerp(char *self, void *, unsigned char value);
unsigned char __fastcall AIVehiclePursuitGetDamagedByPerp(char *self);
unsigned int __fastcall AIVehiclePursuitGetSirenState(char *self);
float __fastcall AIVehiclePursuitGetTimeSinceTargetSeen(char *self);
void __fastcall AIVehiclePursuitZeroTimeSinceTargetSeen(char *self);
char *__fastcall AIVehiclePursuitGetSupportGoal(char *self);
void __fastcall AIVehiclePursuitSetWithinEngagementRadius(char *self);
unsigned char __fastcall AIVehiclePursuitWasWithinEngagementRadius(char *self);
void *__fastcall AIVehiclePursuitDeleteFrom8(char *self, void *, unsigned int flags);
void *__fastcall AIVehiclePursuitDeleteFrom4c(char *self, void *, unsigned int flags);
void *__fastcall AIVehiclePursuitDeleteFrom54(char *self, void *, unsigned int flags);
void *__fastcall AIVehiclePursuitDeleteFrom758(char *self, void *, unsigned int flags);
float __fastcall AIVehicleHelicopterGetDesiredHeightOverDest(char *self);
void __fastcall AIVehicleHelicopterSetDesiredHeightOverDest(char *self, void *, unsigned int value);
unsigned char __fastcall AIVehicleHelicopterStrafeToDestIsSet(char *self);
void __fastcall AIVehicleHelicopterSetStrafeToDest(char *self, void *, unsigned char value);
char *__fastcall AIVehicleHelicopterGetHeliSheetCoord(char *self);
float __fastcall AIVehicleHelicopterGetFuelTimeRemaining(char *self);
void __fastcall AIVehicleHelicopterSetDustStormIntensity(char *self, void *, unsigned int value);
float __fastcall AIVehicleHelicopterGetDustStormIntensity(char *self);
void *__fastcall AIVehicleHelicopterDeleteFrom8(char *self, void *, unsigned int flags);
void *__fastcall AIVehicleHelicopterDeleteFrom4c(char *self, void *, unsigned int flags);
void *__fastcall AIVehicleHelicopterDeleteFrom54(char *self, void *, unsigned int flags);
void *__fastcall AIVehicleHelicopterDeleteFrom758(char *self, void *, unsigned int flags);
void *__fastcall AIVehicleHelicopterDeleteFrom7a4(char *self, void *, unsigned int flags);
unsigned int __fastcall AIRoadBlockGetPursuit(char *self);
void __fastcall AIRoadBlockSetDodged(char *self, void *, unsigned char value);
unsigned char __fastcall AIRoadBlockGetDodged(char *self);
void __fastcall AIRoadBlockIncNumCopsDestroyed(char *self);
unsigned short __fastcall AIRoadBlockGetNumCopsDestroyed(char *self);
void __fastcall AIRoadBlockIncNumCopsDamaged(char *self);
unsigned short __fastcall AIRoadBlockGetNumCopsDamaged(char *self);
char *__fastcall AIRoadBlockGetRoadBlockCentre(char *self);
char *__fastcall AIRoadBlockGetRoadBlockDir(char *self);
unsigned short __fastcall AIRoadBlockGetNumSpikeStrips(char *self);
char *__fastcall AIRoadBlockGetVehicles(char *self);
char *__fastcall AIRoadBlockGetSmackables(char *self);
unsigned char __fastcall AIRoadBlockIsPerpCheating(char *self);
void *__fastcall AIRoadBlockDeleteFrom8(char *self, void *, unsigned int flags);
void *__fastcall AIRoadBlockDeleteFrom2c(char *self, void *, unsigned int flags);
void *__fastcall AIRoadBlockDeleteFrom38(char *self, void *, unsigned int flags);
void *__fastcall AIRoadBlockDeleteFrom48(char *self, void *, unsigned int flags);
void *__fastcall AICopManagerDelete(char *self, void *, unsigned int flags);
unsigned char __fastcall AIPerpVehicleIsHiddenFromCars(char *self);
unsigned char __fastcall AIPerpVehicleIsHiddenFromHelicopters(char *self);
unsigned int __fastcall AIPerpVehicleGetPendingRepPointsNormal(char *self);
unsigned int __fastcall AIPerpVehicleGetPendingRepPointsFromCopDestruction(char *self);
void __fastcall AIPerpVehicleClearPendingRepPoints(char *self);
unsigned int __fastcall AIPerpVehicleGetPursuitEscalationAttrib(char *self);
unsigned int __fastcall AIPerpVehicleGetPursuitLevelAttrib(char *self);
unsigned int __fastcall AIPerpVehicleGetPursuitSupportAttrib(char *self);
unsigned int __fastcall AIPerpVehicleGetRacerInfo(char *self);
float __fastcall AIPerpVehicleGet911CallTime(char *self);
void *__fastcall AIPerpVehicleDeleteFrom8(char *self, void *, unsigned int flags);
void *__fastcall AIPerpVehicleDeleteFrom4c(char *self, void *, unsigned int flags);
void *__fastcall AIPerpVehicleDeleteFrom54(char *self, void *, unsigned int flags);
void *__fastcall AIPerpVehicleDeleteFrom758(char *self, void *, unsigned int flags);
void *__fastcall AIPerpVehicleDeleteFrom760(char *self, void *, unsigned int flags);
void *__fastcall AIPerpVehicleDeleteFrom76c(char *self, void *, unsigned int flags);
float __fastcall AIVehicleCopCarGetSkill(char *);
float __fastcall AIVehicleCopCarGetShortcutSkill(char *);
void *__fastcall AIVehicleCopCarDeleteFrom8(char *self, void *, unsigned int flags);
void *__fastcall AIVehicleCopCarDeleteFrom4c(char *self, void *, unsigned int flags);
void *__fastcall AIVehicleCopCarDeleteFrom54(char *self, void *, unsigned int flags);
void *__fastcall AIVehicleCopCarDeleteFrom758(char *self, void *, unsigned int flags);
void *__fastcall AIVehiclePursuitDelete(char *self, void *, unsigned int flags);
void *__fastcall AIVehicleHelicopterDelete(char *self, void *, unsigned int flags);
unsigned char __fastcall AIPursuitIsPerpBusted(char *self);
float __fastcall AIPursuitGetPursuitDuration(char *self);
float __fastcall AIPursuitGetEvadeLevel(char *self);
float __fastcall AIPursuitGetCoolDownTimeRemaining(char *self);
float __fastcall AIPursuitGetCoolDownTimeRequired(char *self);
float __fastcall AIPursuitGetBackupETA(char *self);
unsigned char __fastcall AIPursuitIsPerpInSight(char *self);
unsigned char __fastcall AIPursuitIsPursuitBailed(char *self);
unsigned char __fastcall AIPursuitIsCollapseActive(char *self);
char *__fastcall AIPursuitGetLastKnownLocation(char *self);
unsigned int __fastcall AIPursuitGetRoadBlock(char *self);
unsigned int __fastcall AIPursuitGetNumCopsDestroyed(char *self);
unsigned int __fastcall AIPursuitGetNumCopsDamaged(char *self);
unsigned int __fastcall AIPursuitGetTotalNumCopsInvolved(char *self);
unsigned int __fastcall AIPursuitGetNumCopsFullyEngaged(char *self);
void __fastcall AIPursuitNotifyRoadblockDodged(char *self);
void __fastcall AIPursuitNotifyRoadblockDeployed(char *self);
void __fastcall AIPursuitNotifyTrafficCarHit(char *self);
void __fastcall AIPursuitNotifySpikeStripDeployed(char *self);
void __fastcall AIPursuitNotifyCopCarDeployed(char *self);
void __fastcall AIPursuitNotifySupportVehicleDeployed(char *self);
unsigned int __fastcall AIPursuitGetNumRoadblocksDodged(char *self);
unsigned int __fastcall AIPursuitGetNumRoadblocksDeployed(char *self);
unsigned int __fastcall AIPursuitGetValueOfPropertyDamaged(char *self);
unsigned int __fastcall AIPursuitGetNumPropertyDamaged(char *self);
unsigned int __fastcall AIPursuitGetNumTrafficCarsHit(char *self);
unsigned int __fastcall AIPursuitGetNumSpikeStripsDodged(char *self);
unsigned int __fastcall AIPursuitGetNumSpikeStripsDeployed(char *self);
unsigned int __fastcall AIPursuitGetNumHeliSpikeStripDeployed(char *self);
unsigned int __fastcall AIPursuitGetNumCopCarsDeployed(char *self);
unsigned int __fastcall AIPursuitGetNumSupportVehiclesDeployed(char *self);
unsigned int __fastcall AIPursuitGetPursuitStatus(char *self);
float __fastcall AIPursuitGetTimeToBackupSpawned(char *self);
unsigned char __fastcall AIPursuitPendingRoadBlockRequest(char *self);
unsigned int __fastcall AIPursuitGetNumHeliSpawns(char *self);
unsigned int __fastcall AIPursuitGetCopDestroyedBonusMultiplier(char *self);
unsigned int __fastcall AIPursuitGetMostRecentCopDestroyedRepPoints(char *self);
unsigned int *__fastcall AIPursuitGetMostRecentCopDestroyedType(char *self, void *, unsigned int *out);
unsigned int __fastcall AIPursuitGetNumCopsInWave(char *self);
unsigned char __fastcall AIPursuitGetIsAJerk(char *self);
float __fastcall AIPursuitGetMinDistanceToTarget(char *self);
void __fastcall AIPursuitSetBustedTimerToZero(char *self);
unsigned char __fastcall AIPursuitGetEnterSafehouseOnDone(char *self);
void *__fastcall AIPursuitDeleteFrom8(char *self, void *, unsigned int flags);
void *__fastcall AIPursuitDeleteFrom2c(char *self, void *, unsigned int flags);
void *__fastcall AIPursuitDeleteFrom38(char *self, void *, unsigned int flags);
void *__fastcall AIPursuitDeleteFrom48(char *self, void *, unsigned int flags);
unsigned int __fastcall AIPursuitGetNumCops(char *self);
void *__fastcall AIRoadBlockDelete(char *self, void *, unsigned int flags);
void *__fastcall AIPerpVehicleDelete(char *self, void *, unsigned int flags);
void *__fastcall AIVehicleCopCarDelete(char *self, void *, unsigned int flags);
void *__fastcall AIPursuitDelete(char *self, void *, unsigned int flags);
void *__fastcall AIGoalPursuitDelete(char *self, void *, unsigned int flags);
void *__fastcall AIGoalPullOverDelete(char *self, void *, unsigned int flags);
void __fastcall PursuitFormationUpdate(char *, void *, unsigned int, unsigned int);
float __fastcall PursuitFormationGetFinisherTolerance(char *);
float __fastcall PursuitFormationGetFinisherTime(char *);
float __fastcall PursuitFormationGetTimeToFinisher(char *);
void *__fastcall PursuitFormationDelete(char *self, void *, unsigned int flags);
float __fastcall BoxInFormationGetFinisherTime(char *self);
void *__fastcall BoxInFormationDelete(char *self, void *, unsigned int flags);
float __fastcall RollingBlockFormationGetFinisherTime(char *self);
void *__fastcall RollingBlockFormationDelete(char *self, void *, unsigned int flags);
void *__fastcall FollowFormationDelete(char *self, void *, unsigned int flags);
void *__fastcall StaggerFollowFormationDelete(char *self, void *, unsigned int flags);
float __fastcall PitFormationGetTimeToFinisher(char *);
float __fastcall PitFormationGetFinisherTolerance(char *);
void *__fastcall PitFormationDelete(char *self, void *, unsigned int flags);
void *__fastcall HerdFormationDelete(char *self, void *, unsigned int flags);
char *__fastcall AttribGenpursuitlevelsEvadeSuccessHeatAdjust(char *self);
char *__fastcall AttribGenpursuitlevelsEventWinHeatAdjust(char *self);
void __fastcall SpikeStripReset(char *, void *);
const void *__fastcall AttribGenpursuitlevelsCTSFor911(void *self);
const void *__fastcall AttribGenpursuitlevelsLifetime911(void *self);
const void *__fastcall AttribGenpursuitlevelsNumCiviHitsFor911(void *self);

const void *__fastcall AttribGenpursuitlevelsCollapseInnerRadius(void *self)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self, 0x1e0af662, 0);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0x4);
}

const void *__fastcall AttribGenpursuitlevelsCollapseOuterRadius(void *self)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self, 0x947542f2, 0);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0x4);
}

const void *__fastcall AttribGenpursuitlevelsCopFormations(void *self, void *, unsigned int index)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self, 0x5c2a7972, index);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0xc);
}

const void *__fastcall AttribGenpursuitlevelsMaxCopsCollapsing(void *self)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self, 0xc77d463, 0);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0x4);
}

const void *__fastcall AttribGenpursuitlevelsScaleEscalationPerBucket(void *self, void *, unsigned int index)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self, 0x80deb840, index);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0x4);
}

const void *__fastcall AttribGenpursuitlevelsSearchModeHeliSpawnChance(void *self)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self, 0x3f11fbfc, 0);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0x4);
}

const void *__fastcall AttribGenpursuitlevelsStaggerFormationTime(void *self)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self, 0x7648c884, 0);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0x4);
}

const void *__fastcall AttribGenpursuitlevelsTimeBetweenFirstFourSpawn(void *self)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self, 0x9bf0f433, 0);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0x4);
}

const void *__fastcall AttribGenpursuitsupportHeavySupportOptions(void *self, void *, unsigned int index)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self, 0xa73c3512, index);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0x10);
}

const void *__fastcall AttribGenpursuitsupportLeaderSupportOptions(void *self, void *, unsigned int index)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self, 0xebd53935, index);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0x14);
}

const void *__fastcall AttribGenpursuitsupportMinimumSupportDelay(void *self)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self, 0xe4e4bc48, 0);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0x4);
}

void *__fastcall IPursuitAIDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x4038f0, void(__thiscall *)(void *))(self);
    if (flags & 1)
        NFS_ORIGINAL(0x652b00, void(__cdecl *)(void *))(self);
    return self;
}

void *__fastcall IAIHelicopterDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x404070, void(__thiscall *)(void *))(self);
    if (flags & 1)
        NFS_ORIGINAL(0x652b00, void(__cdecl *)(void *))(self);
    return self;
}

void *__fastcall ICheaterDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x405250, void(__thiscall *)(void *))(self);
    if (flags & 1)
        NFS_ORIGINAL(0x652b00, void(__cdecl *)(void *))(self);
    return self;
}

unsigned int *__cdecl MUnspawnCopGetKind(unsigned int *kind)
{
    unsigned char &guard = *reinterpret_cast<unsigned char *>(0x90d90c);
    unsigned int &cache = *reinterpret_cast<unsigned int *>(0x90d908);
    if (!(guard & 1))
    {
        guard |= 1;
        cache = stringhash32(reinterpret_cast<const char *>(0x890c24));
    }
    *kind = cache;
    return kind;
}

unsigned int *__cdecl MSetCopsEnabledGetKind(unsigned int *kind)
{
    unsigned char &guard = *reinterpret_cast<unsigned char *>(0x90d91c);
    unsigned int &cache = *reinterpret_cast<unsigned int *>(0x90d918);
    if (!(guard & 1))
    {
        guard |= 1;
        cache = stringhash32(reinterpret_cast<const char *>(0x890c48));
    }
    *kind = cache;
    return kind;
}

unsigned int *__cdecl MForcePursuitStartGetKind(unsigned int *kind)
{
    unsigned char &guard = *reinterpret_cast<unsigned char *>(0x90d924);
    unsigned int &cache = *reinterpret_cast<unsigned int *>(0x90d920);
    if (!(guard & 1))
    {
        guard |= 1;
        cache = stringhash32(reinterpret_cast<const char *>(0x890c58));
    }
    *kind = cache;
    return kind;
}

unsigned int *__cdecl MReqRoadBlockGetKind(unsigned int *kind)
{
    unsigned char &guard = *reinterpret_cast<unsigned char *>(0x90d934);
    unsigned int &cache = *reinterpret_cast<unsigned int *>(0x90d930);
    if (!(guard & 1))
    {
        guard |= 1;
        cache = stringhash32(reinterpret_cast<const char *>(0x890c80));
    }
    *kind = cache;
    return kind;
}

void __fastcall AICopManagerMessageSetAutoSpawnMode(char *, void *, unsigned int)
{
}

void __fastcall AICopManagerNoNewPursuitsOrCops(char *self)
{
    *reinterpret_cast<unsigned char *>(self + 0x60) = 1;
}

void __fastcall AICopManagerOnDebugDraw(char *, void *)
{
}

void __fastcall AIActionStaticRoadBlockBeginAction(char *, void *, unsigned int)
{
}

void __fastcall AIActionStaticRoadBlockFinishAction(char *, void *, unsigned int)
{
}

void __fastcall AIActionStaticRoadBlockOnDebugDraw(char *, void *)
{
}

unsigned int *__cdecl MPerpBustedGetKind(unsigned int *kind)
{
    unsigned char &guard = *reinterpret_cast<unsigned char *>(0x90d964);
    unsigned int &cache = *reinterpret_cast<unsigned int *>(0x90d960);
    if (!(guard & 1))
    {
        guard |= 1;
        cache = stringhash32(reinterpret_cast<const char *>(0x890d98));
    }
    *kind = cache;
    return kind;
}

void __fastcall AIPerpVehicleSetCostToState(char *self, void *, unsigned int value)
{
    *reinterpret_cast<unsigned int *>(self + 0x20) = value;
}

unsigned int __fastcall AIPerpVehicleGetCostToState(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x20);
}

float __fastcall AIPerpVehicleGetHeat(char *self)
{
    return *reinterpret_cast<float *>(self + 0x1c);
}

bool __fastcall AIPerpVehicleOnClearCausality(char *, void *, unsigned int)
{
    return false;
}

float __fastcall AIPerpVehicleGetLastTrafficHitTime(char *self)
{
    return *reinterpret_cast<float *>(self + 0x64);
}

void __fastcall AIVehiclePursuitSetSupportGoal(char *self, void *, unsigned int value)
{
    *reinterpret_cast<unsigned int *>(self + 0x48) = value;
}

bool __cdecl HeliVehicleActive()
{
    return *reinterpret_cast<void *const *>(0x90d61c) != nullptr;
}

unsigned int *__cdecl MNotifyPursuitLengthGetKind(unsigned int *kind)
{
    unsigned char &guard = *reinterpret_cast<unsigned char *>(0x90d96c);
    unsigned int &cache = *reinterpret_cast<unsigned int *>(0x90d968);
    if (!(guard & 1))
    {
        guard |= 1;
        cache = stringhash32(reinterpret_cast<const char *>(0x890e10));
    }
    *kind = cache;
    return kind;
}

unsigned int __fastcall AIPursuitGetFormationType(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0xc0);
}

unsigned int __fastcall AIPursuitGetTarget(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x74);
}

bool __fastcall AIPursuitIsAttemptingRoadBlock(char *self)
{
    return *reinterpret_cast<void **>(self + 0xcc) != nullptr;
}

void __fastcall AIPursuitOnDebugDraw(char *, void *)
{
}

void *__fastcall ICopMgrDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x410770, void(__thiscall *)(void *))(self);
    if (flags & 1)
        NFS_ORIGINAL(0x652b00, void(__cdecl *)(void *))(self);
    return self;
}

bool __fastcall AICopManagerIsCopSpawnPending(char *self)
{
    return *reinterpret_cast<void **>(self + 0x1c) != nullptr;
}

void *__fastcall AIActionPursuitOffRoadDeleteFrom8(char *self, void *, unsigned int flags)
{
    self -= 0x8;
    NFS_ORIGINAL(0x420370, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0xc0, nullptr);
    return self;
}

void *__fastcall AIActionHeliPursuitDeleteFrom8(char *self, void *, unsigned int flags)
{
    self -= 0x8;
    NFS_ORIGINAL(0x421040, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0xa8, nullptr);
    return self;
}

void *__fastcall AIActionHeliExitDeleteFrom8(char *self, void *, unsigned int flags)
{
    self -= 0x8;
    NFS_ORIGINAL(0x4212d0, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x74, nullptr);
    return self;
}

void *__fastcall AIActionRamDeleteFrom8(char *self, void *, unsigned int flags)
{
    self -= 0x8;
    NFS_ORIGINAL(0x421780, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x68, nullptr);
    return self;
}

bool __fastcall AIActionStaticRoadBlockIsFinished(char *, void *)
{
    return false;
}

void *__fastcall AIActionStaticRoadBlockDeleteFrom8(char *self, void *, unsigned int flags)
{
    self -= 0x8;
    NFS_ORIGINAL(0x421f90, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x50, nullptr);
    return self;
}

void *__fastcall IPursuitDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x42e1b0, void(__thiscall *)(void *))(self);
    if (flags & 1)
        NFS_ORIGINAL(0x652b00, void(__cdecl *)(void *))(self);
    return self;
}

void *__fastcall IRoadBlockDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x42e260, void(__thiscall *)(void *))(self);
    if (flags & 1)
        NFS_ORIGINAL(0x652b00, void(__cdecl *)(void *))(self);
    return self;
}

unsigned int __fastcall AICopManagerGetCacheName(char *)
{
    return 0x891664;
}

float __fastcall AICopManagerGetLockoutTimeRemaining(char *self)
{
    return *reinterpret_cast<float *>(self + 0x58);
}

void *__fastcall AICopManagerDeleteFrom8(char *self, void *, unsigned int flags)
{
    return AICopManagerDelete(self - 0x8, nullptr, flags);
}

void *__fastcall AICopManagerDeleteFrom2c(char *self, void *, unsigned int flags)
{
    return AICopManagerDelete(self - 0x2c, nullptr, flags);
}

void *__fastcall AICopManagerDeleteFrom38(char *self, void *, unsigned int flags)
{
    return AICopManagerDelete(self - 0x38, nullptr, flags);
}

void *__fastcall AICopManagerDeleteFrom48(char *self, void *, unsigned int flags)
{
    return AICopManagerDelete(self - 0x48, nullptr, flags);
}

void *__fastcall AICopManagerDeleteFrom54(char *self, void *, unsigned int flags)
{
    return AICopManagerDelete(self - 0x54, nullptr, flags);
}

void *__fastcall AICopManagerDeleteFrom5c(char *self, void *, unsigned int flags)
{
    return AICopManagerDelete(self - 0x5c, nullptr, flags);
}

void __fastcall AIVehiclePursuitSetInPursuit(char *self, void *, unsigned char value)
{
    *reinterpret_cast<unsigned char *>(self + 0x8) = value;
}

unsigned char __fastcall AIVehiclePursuitGetInPursuit(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0x8);
}

void __fastcall AIVehiclePursuitSetInFormation(char *self, void *, unsigned char value)
{
    *reinterpret_cast<unsigned char *>(self + 0x20) = value;
}

unsigned char __fastcall AIVehiclePursuitGetInFormation(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0x20);
}

void __fastcall AIVehiclePursuitSetInPosition(char *self, void *, unsigned char value)
{
    *reinterpret_cast<unsigned char *>(self + 0x21) = value;
}

unsigned char __fastcall AIVehiclePursuitGetInPosition(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0x21);
}

char *__fastcall AIVehiclePursuitGetPursuitOffset(char *self)
{
    return self + 0x24;
}

char *__fastcall AIVehiclePursuitGetInPositionOffset(char *self)
{
    return self + 0x30;
}

void __fastcall AIVehiclePursuitSetInPositionGoal(char *self, void *, const unsigned int *value)
{
    *reinterpret_cast<unsigned int *>(self + 0x3c) = *value;
}

char *__fastcall AIVehiclePursuitGetInPositionGoal(char *self)
{
    return self + 0x3c;
}

void __fastcall AIVehiclePursuitSetBreaker(char *self, void *, unsigned char value)
{
    *reinterpret_cast<unsigned char *>(self + 0x9) = value;
}

unsigned char __fastcall AIVehiclePursuitGetBreaker(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0x9);
}

void __fastcall AIVehiclePursuitSetChicken(char *self, void *, unsigned char value)
{
    *reinterpret_cast<unsigned char *>(self + 0xa) = value;
}

unsigned char __fastcall AIVehiclePursuitGetChicken(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0xa);
}

void __fastcall AIVehiclePursuitSetDamagedByPerp(char *self, void *, unsigned char value)
{
    *reinterpret_cast<unsigned char *>(self + 0xb) = value;
}

unsigned char __fastcall AIVehiclePursuitGetDamagedByPerp(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0xb);
}

unsigned int __fastcall AIVehiclePursuitGetSirenState(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0xc);
}

float __fastcall AIVehiclePursuitGetTimeSinceTargetSeen(char *self)
{
    return *reinterpret_cast<float *>(self + 0x40);
}

void __fastcall AIVehiclePursuitZeroTimeSinceTargetSeen(char *self)
{
    *reinterpret_cast<unsigned int *>(self + 0x40) = 0;
}

char *__fastcall AIVehiclePursuitGetSupportGoal(char *self)
{
    return self + 0x48;
}

void __fastcall AIVehiclePursuitSetWithinEngagementRadius(char *self)
{
    *reinterpret_cast<unsigned char *>(self + 0x22) = 1;
}

unsigned char __fastcall AIVehiclePursuitWasWithinEngagementRadius(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0x22);
}

void *__fastcall AIVehiclePursuitDeleteFrom8(char *self, void *, unsigned int flags)
{
    return AIVehiclePursuitDelete(self - 0x8, nullptr, flags);
}

void *__fastcall AIVehiclePursuitDeleteFrom4c(char *self, void *, unsigned int flags)
{
    return AIVehiclePursuitDelete(self - 0x4c, nullptr, flags);
}

void *__fastcall AIVehiclePursuitDeleteFrom54(char *self, void *, unsigned int flags)
{
    return AIVehiclePursuitDelete(self - 0x54, nullptr, flags);
}

void *__fastcall AIVehiclePursuitDeleteFrom758(char *self, void *, unsigned int flags)
{
    return AIVehiclePursuitDelete(self - 0x758, nullptr, flags);
}

float __fastcall AIVehicleHelicopterGetDesiredHeightOverDest(char *self)
{
    return *reinterpret_cast<float *>(self + 0x2c);
}

void __fastcall AIVehicleHelicopterSetDesiredHeightOverDest(char *self, void *, unsigned int value)
{
    *reinterpret_cast<unsigned int *>(self + 0x2c) = value;
}

unsigned char __fastcall AIVehicleHelicopterStrafeToDestIsSet(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0x30);
}

void __fastcall AIVehicleHelicopterSetStrafeToDest(char *self, void *, unsigned char value)
{
    *reinterpret_cast<unsigned char *>(self + 0x30) = value;
}

char *__fastcall AIVehicleHelicopterGetHeliSheetCoord(char *self)
{
    return self + 0x7f0;
}

float __fastcall AIVehicleHelicopterGetFuelTimeRemaining(char *self)
{
    return *reinterpret_cast<float *>(self + 0x34);
}

void __fastcall AIVehicleHelicopterSetDustStormIntensity(char *self, void *, unsigned int value)
{
    *reinterpret_cast<unsigned int *>(self + 0x3c) = value;
}

float __fastcall AIVehicleHelicopterGetDustStormIntensity(char *self)
{
    return *reinterpret_cast<float *>(self + 0x3c);
}

void *__fastcall AIVehicleHelicopterDeleteFrom8(char *self, void *, unsigned int flags)
{
    return AIVehicleHelicopterDelete(self - 0x8, nullptr, flags);
}

void *__fastcall AIVehicleHelicopterDeleteFrom4c(char *self, void *, unsigned int flags)
{
    return AIVehicleHelicopterDelete(self - 0x4c, nullptr, flags);
}

void *__fastcall AIVehicleHelicopterDeleteFrom54(char *self, void *, unsigned int flags)
{
    return AIVehicleHelicopterDelete(self - 0x54, nullptr, flags);
}

void *__fastcall AIVehicleHelicopterDeleteFrom758(char *self, void *, unsigned int flags)
{
    return AIVehicleHelicopterDelete(self - 0x758, nullptr, flags);
}

void *__fastcall AIVehicleHelicopterDeleteFrom7a4(char *self, void *, unsigned int flags)
{
    return AIVehicleHelicopterDelete(self - 0x7a4, nullptr, flags);
}

unsigned int __fastcall AIRoadBlockGetPursuit(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x28);
}

void __fastcall AIRoadBlockSetDodged(char *self, void *, unsigned char value)
{
    *reinterpret_cast<unsigned char *>(self + 0x2c) = value;
}

unsigned char __fastcall AIRoadBlockGetDodged(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0x2c);
}

void __fastcall AIRoadBlockIncNumCopsDestroyed(char *self)
{
    ++*reinterpret_cast<short *>(self + 0x30);
}

unsigned short __fastcall AIRoadBlockGetNumCopsDestroyed(char *self)
{
    return *reinterpret_cast<unsigned short *>(self + 0x30);
}

void __fastcall AIRoadBlockIncNumCopsDamaged(char *self)
{
    ++*reinterpret_cast<short *>(self + 0x2e);
}

unsigned short __fastcall AIRoadBlockGetNumCopsDamaged(char *self)
{
    return *reinterpret_cast<unsigned short *>(self + 0x2e);
}

char *__fastcall AIRoadBlockGetRoadBlockCentre(char *self)
{
    return self + 0x34;
}

char *__fastcall AIRoadBlockGetRoadBlockDir(char *self)
{
    return self + 0x40;
}

unsigned short __fastcall AIRoadBlockGetNumSpikeStrips(char *self)
{
    return *reinterpret_cast<unsigned short *>(self + 0x32);
}

char *__fastcall AIRoadBlockGetVehicles(char *self)
{
    return self + 0x8;
}

char *__fastcall AIRoadBlockGetSmackables(char *self)
{
    return self + 0x18;
}

unsigned char __fastcall AIRoadBlockIsPerpCheating(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0x5c);
}

void *__fastcall AIRoadBlockDeleteFrom8(char *self, void *, unsigned int flags)
{
    return AIRoadBlockDelete(self - 0x8, nullptr, flags);
}

void *__fastcall AIRoadBlockDeleteFrom2c(char *self, void *, unsigned int flags)
{
    return AIRoadBlockDelete(self - 0x2c, nullptr, flags);
}

void *__fastcall AIRoadBlockDeleteFrom38(char *self, void *, unsigned int flags)
{
    return AIRoadBlockDelete(self - 0x38, nullptr, flags);
}

void *__fastcall AIRoadBlockDeleteFrom48(char *self, void *, unsigned int flags)
{
    return AIRoadBlockDelete(self - 0x48, nullptr, flags);
}

void *__fastcall AICopManagerDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x430af0, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x15c, nullptr);
    return self;
}

unsigned char __fastcall AIPerpVehicleIsHiddenFromCars(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0x2c);
}

unsigned char __fastcall AIPerpVehicleIsHiddenFromHelicopters(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0x2d);
}

unsigned int __fastcall AIPerpVehicleGetPendingRepPointsNormal(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x24);
}

unsigned int __fastcall AIPerpVehicleGetPendingRepPointsFromCopDestruction(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x28);
}

void __fastcall AIPerpVehicleClearPendingRepPoints(char *self)
{
    *reinterpret_cast<unsigned int *>(self + 0x24) = 0;
    *reinterpret_cast<unsigned int *>(self + 0x28) = 0;
}

unsigned int __fastcall AIPerpVehicleGetPursuitEscalationAttrib(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x48);
}

unsigned int __fastcall AIPerpVehicleGetPursuitLevelAttrib(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x40);
}

unsigned int __fastcall AIPerpVehicleGetPursuitSupportAttrib(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x44);
}

unsigned int __fastcall AIPerpVehicleGetRacerInfo(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x4c);
}

float __fastcall AIPerpVehicleGet911CallTime(char *self)
{
    return *reinterpret_cast<float *>(self + 0x68);
}

void *__fastcall AIPerpVehicleDeleteFrom8(char *self, void *, unsigned int flags)
{
    return AIPerpVehicleDelete(self - 0x8, nullptr, flags);
}

void *__fastcall AIPerpVehicleDeleteFrom4c(char *self, void *, unsigned int flags)
{
    return AIPerpVehicleDelete(self - 0x4c, nullptr, flags);
}

void *__fastcall AIPerpVehicleDeleteFrom54(char *self, void *, unsigned int flags)
{
    return AIPerpVehicleDelete(self - 0x54, nullptr, flags);
}

void *__fastcall AIPerpVehicleDeleteFrom758(char *self, void *, unsigned int flags)
{
    return AIPerpVehicleDelete(self - 0x758, nullptr, flags);
}

void *__fastcall AIPerpVehicleDeleteFrom760(char *self, void *, unsigned int flags)
{
    return AIPerpVehicleDelete(self - 0x760, nullptr, flags);
}

void *__fastcall AIPerpVehicleDeleteFrom76c(char *self, void *, unsigned int flags)
{
    return AIPerpVehicleDelete(self - 0x76c, nullptr, flags);
}

float __fastcall AIVehicleCopCarGetSkill(char *)
{
    return 1.0f;
}

float __fastcall AIVehicleCopCarGetShortcutSkill(char *)
{
    return 1.0f;
}

void *__fastcall AIVehicleCopCarDeleteFrom8(char *self, void *, unsigned int flags)
{
    return AIVehicleCopCarDelete(self - 0x8, nullptr, flags);
}

void *__fastcall AIVehicleCopCarDeleteFrom4c(char *self, void *, unsigned int flags)
{
    return AIVehicleCopCarDelete(self - 0x4c, nullptr, flags);
}

void *__fastcall AIVehicleCopCarDeleteFrom54(char *self, void *, unsigned int flags)
{
    return AIVehicleCopCarDelete(self - 0x54, nullptr, flags);
}

void *__fastcall AIVehicleCopCarDeleteFrom758(char *self, void *, unsigned int flags)
{
    return AIVehicleCopCarDelete(self - 0x758, nullptr, flags);
}

void *__fastcall AIVehiclePursuitDelete(char *self, void *, unsigned int flags)
{
    AIVehiclePursuitDestruct(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x7a4, nullptr);
    return self;
}

void *__fastcall AIVehicleHelicopterDelete(char *self, void *, unsigned int flags)
{
    AIVehicleHelicopterDestruct(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x8c0, nullptr);
    return self;
}

unsigned char __fastcall AIPursuitIsPerpBusted(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0xe8);
}

float __fastcall AIPursuitGetPursuitDuration(char *self)
{
    return *reinterpret_cast<float *>(self + 0xb4);
}

float __fastcall AIPursuitGetEvadeLevel(char *self)
{
    return *reinterpret_cast<float *>(self + 0xfc);
}

float __fastcall AIPursuitGetCoolDownTimeRemaining(char *self)
{
    return *reinterpret_cast<float *>(self + 0x100);
}

float __fastcall AIPursuitGetCoolDownTimeRequired(char *self)
{
    return *reinterpret_cast<float *>(self + 0x104) - 7.0f;
}

float __fastcall AIPursuitGetBackupETA(char *self)
{
    return *reinterpret_cast<float *>(self + 0x21c);
}

unsigned char __fastcall AIPursuitIsPerpInSight(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0x114);
}

unsigned char __fastcall AIPursuitIsPursuitBailed(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0xe9);
}

unsigned char __fastcall AIPursuitIsCollapseActive(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0xb8);
}

char *__fastcall AIPursuitGetLastKnownLocation(char *self)
{
    return self + 0x118;
}

unsigned int __fastcall AIPursuitGetRoadBlock(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x84);
}

unsigned int __fastcall AIPursuitGetNumCopsDestroyed(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x13c);
}

unsigned int __fastcall AIPursuitGetNumCopsDamaged(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x15c);
}

unsigned int __fastcall AIPursuitGetTotalNumCopsInvolved(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x138);
}

unsigned int __fastcall AIPursuitGetNumCopsFullyEngaged(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x10c);
}

void __fastcall AIPursuitNotifyRoadblockDodged(char *self)
{
    ++*reinterpret_cast<int *>(self + 0x154);
}

void __fastcall AIPursuitNotifyRoadblockDeployed(char *self)
{
    ++*reinterpret_cast<int *>(self + 0x158);
}

void __fastcall AIPursuitNotifyTrafficCarHit(char *self)
{
    ++*reinterpret_cast<int *>(self + 0x168);
}

void __fastcall AIPursuitNotifySpikeStripDeployed(char *self)
{
    ++*reinterpret_cast<int *>(self + 0x17c);
}

void __fastcall AIPursuitNotifyCopCarDeployed(char *self)
{
    ++*reinterpret_cast<int *>(self + 0x184);
}

void __fastcall AIPursuitNotifySupportVehicleDeployed(char *self)
{
    ++*reinterpret_cast<int *>(self + 0x188);
}

unsigned int __fastcall AIPursuitGetNumRoadblocksDodged(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x154);
}

unsigned int __fastcall AIPursuitGetNumRoadblocksDeployed(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x158);
}

unsigned int __fastcall AIPursuitGetValueOfPropertyDamaged(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x174);
}

unsigned int __fastcall AIPursuitGetNumPropertyDamaged(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x178);
}

unsigned int __fastcall AIPursuitGetNumTrafficCarsHit(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x168);
}

unsigned int __fastcall AIPursuitGetNumSpikeStripsDodged(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x16c);
}

unsigned int __fastcall AIPursuitGetNumSpikeStripsDeployed(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x17c);
}

unsigned int __fastcall AIPursuitGetNumHeliSpikeStripDeployed(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x180);
}

unsigned int __fastcall AIPursuitGetNumCopCarsDeployed(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x184);
}

unsigned int __fastcall AIPursuitGetNumSupportVehiclesDeployed(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x188);
}

unsigned int __fastcall AIPursuitGetPursuitStatus(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x218);
}

float __fastcall AIPursuitGetTimeToBackupSpawned(char *self)
{
    return *reinterpret_cast<float *>(self + 0x21c);
}

unsigned char __fastcall AIPursuitPendingRoadBlockRequest(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0x190);
}

unsigned int __fastcall AIPursuitGetNumHeliSpawns(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x150);
}

unsigned int __fastcall AIPursuitGetCopDestroyedBonusMultiplier(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0xf0);
}

unsigned int __fastcall AIPursuitGetMostRecentCopDestroyedRepPoints(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0xf4);
}

unsigned int *__fastcall AIPursuitGetMostRecentCopDestroyedType(char *self, void *, unsigned int *out)
{
    *out = *reinterpret_cast<unsigned int *>(self + 0xf8);
    return out;
}

unsigned int __fastcall AIPursuitGetNumCopsInWave(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x144);
}

unsigned char __fastcall AIPursuitGetIsAJerk(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0x238);
}

float __fastcall AIPursuitGetMinDistanceToTarget(char *self)
{
    return *reinterpret_cast<float *>(self + 0x220);
}

void __fastcall AIPursuitSetBustedTimerToZero(char *self)
{
    *reinterpret_cast<unsigned int *>(self + 0xdc) = 0;
}

unsigned char __fastcall AIPursuitGetEnterSafehouseOnDone(char *self)
{
    return *reinterpret_cast<unsigned char *>(self + 0x240);
}

void *__fastcall AIPursuitDeleteFrom8(char *self, void *, unsigned int flags)
{
    return AIPursuitDelete(self - 0x8, nullptr, flags);
}

void *__fastcall AIPursuitDeleteFrom2c(char *self, void *, unsigned int flags)
{
    return AIPursuitDelete(self - 0x2c, nullptr, flags);
}

void *__fastcall AIPursuitDeleteFrom38(char *self, void *, unsigned int flags)
{
    return AIPursuitDelete(self - 0x38, nullptr, flags);
}

void *__fastcall AIPursuitDeleteFrom48(char *self, void *, unsigned int flags)
{
    return AIPursuitDelete(self - 0x48, nullptr, flags);
}

unsigned int __fastcall AIPursuitGetNumCops(char *self)
{
    return *reinterpret_cast<unsigned int *>(self + 0x20);
}

void *__fastcall AIRoadBlockDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x432820, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0xa8, nullptr);
    return self;
}

void *__fastcall AIPerpVehicleDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x4331f0, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x7c4, nullptr);
    return self;
}

void *__fastcall AIVehicleCopCarDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x4334b0, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x7ac, nullptr);
    return self;
}

void *__fastcall AIPursuitDelete(char *self, void *, unsigned int flags)
{
    AIPursuitDestruct(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x28c, nullptr);
    return self;
}

void *__fastcall AIGoalPursuitDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x435430, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x1c, nullptr);
    return self;
}

void *__fastcall AIGoalPullOverDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x435440, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x18, nullptr);
    return self;
}

void __fastcall PursuitFormationUpdate(char *, void *, unsigned int, unsigned int)
{
}

float __fastcall PursuitFormationGetFinisherTolerance(char *)
{
    return 1.0f;
}

float __fastcall PursuitFormationGetFinisherTime(char *)
{
    return 2.0f;
}

float __fastcall PursuitFormationGetTimeToFinisher(char *)
{
    return 4.0f;
}

void *__fastcall PursuitFormationDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x438190, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x20, nullptr);
    return self;
}

float __fastcall BoxInFormationGetFinisherTime(char *self)
{
    return *reinterpret_cast<float *>(self + 0x24);
}

void *__fastcall BoxInFormationDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x43f510, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x28, nullptr);
    return self;
}

float __fastcall RollingBlockFormationGetFinisherTime(char *self)
{
    return *reinterpret_cast<float *>(self + 0x24);
}

void *__fastcall RollingBlockFormationDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x43f760, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x28, nullptr);
    return self;
}

void *__fastcall FollowFormationDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x43faf0, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x20, nullptr);
    return self;
}

void *__fastcall StaggerFollowFormationDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x43fe80, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x20, nullptr);
    return self;
}

float __fastcall PitFormationGetTimeToFinisher(char *)
{
    return 1.2f;
}

float __fastcall PitFormationGetFinisherTolerance(char *)
{
    return 0.5f;
}

void *__fastcall PitFormationDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x440040, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x20, nullptr);
    return self;
}

void *__fastcall HerdFormationDelete(char *self, void *, unsigned int flags)
{
    NFS_ORIGINAL(0x440250, void(__thiscall *)(void *))(self);
    if ((flags & 1) && self)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), self, 0x20, nullptr);
    return self;
}

char *__fastcall AttribGenpursuitlevelsEvadeSuccessHeatAdjust(char *self)
{
    return *reinterpret_cast<char **>(self + 8) + 0xa8;
}

char *__fastcall AttribGenpursuitlevelsEventWinHeatAdjust(char *self)
{
    return *reinterpret_cast<char **>(self + 8) + 0x84;
}

void __fastcall SpikeStripReset(char *, void *)
{
}

const void *__fastcall AttribGenpursuitlevelsCTSFor911(void *self)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self, 0xa00de933, 0);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0x4);
}

const void *__fastcall AttribGenpursuitlevelsLifetime911(void *self)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self, 0xe823327, 0);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0x4);
}

const void *__fastcall AttribGenpursuitlevelsNumCiviHitsFor911(void *self)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self, 0x6e590f57, 0);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0x1);
}
