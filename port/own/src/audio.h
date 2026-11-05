/* audio: the sequencer clock and the sound: sample voices (loop modes, slices, wavetable, granular), TrackerXXL synth voices, mixer, FX. */
#pragma once
#include "own.h"

namespace audio
{
void init();
void shutdown();
void render(int16_t* out, int frames);           /* interleaved stereo, 128 frames */
void hostPos(double ppq, double bpm, int playing, int frames);
void setHostSync(bool on);
bool hostSync();

/* transport */
void play(bool song, bool fromStart);
void stop();
bool playing();
bool songMode();
int playStep();                                  /* step being played now (0-based inside its pattern), -1 stopped */
int playPattern();                               /* pattern index being played, -1 stopped */
int playSongSlot();
double tempo();                                  /* bpm in use */
double timeSeconds();
void setEditPattern(int p);                      /* the pattern a pattern-mode play loops */
bool popPatternChange(int& pat);                 /* the song / play position moved to another pattern: UI follows */

/* notes for pads / keyboard / preview.  vel 1..127 */
void noteOn(int track, int note, int instr, int vel);
void noteOff(int track);
void allOff();
void previewSample(const own::Sample* s, int volPct);   /* a file from the explorer; nullptr stops */
bool previewing();
void setPreviewVol(int pct);
void refreshInstrument(int i);                   /* the instrument changed: forget the synth state of the tracks that play it */
void loadPatch(own::Instr& in);                  /* synth: take the starter / bass bank sound's controls into the instrument */
int patchCount(int voice);
const char* patchName(int voice, int i);
int bazzVoice();
float level();                                   /* 0..1 peak of the last blocks */
float samplePlayPos(int track, int instr);       /* 0..1 of the sample being played by that track, -1 if none */
float lastPlayPos();                             /* 0..1 of the most recent sample voice, -1 none */
float dbOf(int vol);
void dbText(int vol, char* b);
void tapTempo();
void setMetronome(bool on, int vol);
}
