#pragma once

/**
 * A car's straight-line performance from its physics collections (source/gameplay/vehicles/Performance.cpp), as the test track
 * measures it (dev/testtrack.h): the model of tools/perfmodel.py, which checks it against measured runs. Not in the
 * original game.
 */
#include "game/gen/api.h"

namespace nfs::performance
{
struct Figures
{
    /** m/s: where the speed stops rising (drag, the redline in top gear or the limiter). */
    float topSpeed;
    /** Seconds from a standstill; negative when not reached. */
    float zeroTo60Mph;
    float zeroTo100Kmh;
    float zeroTo100Mph;
    float zeroTo200Kmh;
    float quarterMile;
    /** At the crank with the boost: W and N m, and the engine speeds (rpm) they peak at. */
    float peakPower;
    float peakPowerRpm;
    float peakTorque;
    float peakTorqueRpm;
    float mass;
    int gears;
    /** The front axle's share of the drive: 0 rear-wheel, 1 front-wheel drive, between all-wheel drive. */
    float frontDrive;
    bool forcedInduction;
    /** How it corners, from 0 (it slides: drifting) to 1 (it grips), from its tyres, balance and power. */
    float grip;
    /** m/s: the most its gearing allows (the redline in top gear, or the limiter), and where its power stops
     * gaining on drag with no redline. */
    float capSpeed;
    float powerSpeed;
};

/** The figures of a pvehicle instance as configured (its upgrades and junkman parts are in its collections); false
 * when it lacks an engine, gearbox or tyres. */
bool Estimate(const Attrib::Instance &pvehicle, Figures &out);
/** The figures shown in the front end: the test track's for the car as it comes, changed by what the model says
 * its upgrades and parts change (the model alone for a car the test track has not measured). */
bool Measured(const Attrib::Instance &pvehicle, Figures &out);
/** Debug.PerfLog: logs every pvehicle collection's stock figures once (for tools/perfmodel.py --game). */
void LogAll();
/** The cars' physics changed (realistic or standard, gameplay/vehicles/realism.h): the measured figures and the bars' scales are
 * read again. */
void Reset();
/** The bars' scales are made again at the next rating (source/gameplay/vehicles/Ratings.cpp). */
void ResetScale();
/** Debug.RatingsCompare: logs the original game's bars next to these for every career car of a car list, as
 * configured (source/gameplay/vehicles/Ratings.cpp). */
void LogCareerRatings(FEPlayerCarDB *cars);
/** The figures of the car the front end last rated (source/gameplay/vehicles/Ratings.cpp); false before any. */
bool Shown(Figures &out);
/** The drift/grip bar: Figures::grip scaled over every car of the performance table, stock, fully upgraded and with
 * junkman everywhere (0 the loosest, 1 the grippiest). */
float GripRating(const Figures &figures);
/** Between these, the first rating the front end makes keeps its figures: a screen brackets the update of its bars
 * to show the details of the car the bars are for. */
void BeginCapture();
bool EndCapture(Figures &out);
} // namespace nfs::performance
