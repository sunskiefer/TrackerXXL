/* grid: the pattern editor's picture (15 rows, 4 tracks wide, cursor on row 7) written into the pattern scene slots. */
#pragma once
#include "own.h"

namespace grid
{
struct View
{
    const own::Pattern* pat = nullptr;
    int first = 0;            /* first visible track (0 or 4) */
    int step = 0;             /* cursor step, 0-based */
    int track = 0;            /* cursor track */
    int param = 0;            /* 0 note, 1 instrument, 2 fx1, 3 fx2 */
    int curState = 1;         /* 0 none, 1 cursor shown */
    int curColor = 1;         /* 0 red (edit), 1 green, 2 white */
    int playhead = -1;        /* playing step, -1 = none */
    const bool* inactive = nullptr;   /* kTracks flags (muted) */
    int stepDiv = 4;
};
void init();
void hide();
void frame(const View& v);
/* a tap on the grid (phRR, zone 1..20): which row (0..14 on screen), track (absolute) and field */
bool decodeTap(int idx, int value, const View& v, int& row, int& track, int& param);
}
