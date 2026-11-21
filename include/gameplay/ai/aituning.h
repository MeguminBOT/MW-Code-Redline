#pragma once

/**
 * Opponents and cops (source/gameplay/ai/AiTuning.cpp, docs/AI.md): the Opponents and Cops page's settings, speed.ini [AI].
 *
 *     Driving = improved | original     improved: the racers corner, brake and accelerate nearer their cars' limits at
 *                                       every skill, look further ahead and use more nitrous; and no AI car stops
 *                                       dead where the world is not loaded around it (source/gameplay/ai/SimplePhysics.cpp),
 *                                       whatever the profile's Vanilla Bug Fixes says
 *     RubberBand = reduced | off | original    how much the game's catch-up ("glue") moves a racer's skill and how much
 *                                       extra acceleration it gives one far behind; reduced keeps a little of it
 *     Cops = improved | original        improved: cops of a lower heat stay a while after the heat rises, see further
 *                                       along a clear line, keep clear of traffic, spawn heavy support out of view and
 *                                       get unstuck sooner; the helicopter's lights are on at night
 *     Traffic = improved | original     improved: traffic drives at each road's own speed (source/gameplay/ai/
 *                                       TrafficSpeed.cpp, [Traffic]): highways 120 km/h, rural highways 90, the
 *                                       outskirts 80, city roads 70, city streets 60; original: 56 km/h on roads under
 *                                       four lanes, 88 on the rest
 */
namespace nfs::aituning
{
enum RubberBand
{
    kRubberOriginal,
    kRubberReduced,
    kRubberOff
};

/** Whether the AI drives as improved (the default). */
bool Improved();
/** Whether the cops' improvements are on (the default). */
bool CopsImproved();
/** Whether traffic drives at the improved road speeds (the default). */
bool TrafficImproved();
/** The rubber band chosen. */
int Rubber();
/** Reads [AI] again and puts the tables in use (the page calls it after a change; the first call is at start). */
void Apply();
} // namespace nfs::aituning
