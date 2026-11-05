/* app: the Tracker's screens, buttons and workflow on top of the model and the audio engine. */
#pragma once
#include <stdint.h>

namespace app
{
void init(const char* root);
void shutdown();
void buttonTap(int n);                      /* Tracker button numbers: 0-7 bottom, 8.. screens, 22 pattern, 23 play ... 32 right */
void buttonPress(int n);
void buttonRelease(int n);
void wheel(int clicks);                     /* the data wheel */
void wheelTab(int screen);                 /* the tab whose wheel control was turned */
void macro(int b, int c);   /* b = screen*8 + index: the tab the macro belongs to and which of its 8 controls */
void macroLabel(int scr, int idx, char* name, char* val, int len);   /* what macro idx (0..7) of screen `scr` controls */
void knob(int clicks, bool coarse);         /* the value Q-Links */
void padDown(int idx, int vel);             /* PLAY mode pads: idx 0..63 = Force pad (row from bottom *8 + col) */
void padUp(int idx);
void stopButton();
void midiNote(int note, int vel, bool on);  /* MIDI keyboard / host notes */
void frame();                               /* write the picture into the scene slots */
void tick(int frames);                      /* audio time passes */
void setPadMode(int m);
}
