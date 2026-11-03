/* TrackerXXL scene table: the flat set of numbered slots the skin shows (one slot = one VST parameter).
 *
 *   state slot  : option / int parameter, value = state (which picture / colour / frame is shown)
 *   text pair   : <key> (state) + <key>_t (16-bit hash of the text, so MPC repaints exactly when the text changes);
 *                 get_param("<key>_t_display") returns the text itself (vst.json: dynamic_display)
 *   input slots : ph.. (pattern touch zones) and lh..m.. (list rows): written by MPC when a finger taps, consumed by the bridge
 *
 * Writers: the bridge on the firmware UI thread.  Readers: get_param() on the host thread.  One mutex, never held across firmware calls.
 * Everything is written only when it CHANGED, and the bridge runs at most 30 times a second: MPC's screen thread pays for every repaint. */
#pragma once
#include <stdint.h>

namespace xxl_scene
{
void init();                                              /* builds the key index from scene_layout.h (SCN_KEYS) */
int index(const char* key);                               /* -1 if not a scene parameter */
int count();
int first();                                              /* SCN_FIRST */

/* bridge side (UI thread) */
bool setState(int idx, int value);                        /* true if it changed */
bool setText(int idxState, const char* text);             /* writes idx+1; true if it changed; empty text allowed */
int  getState(int idx);
void begin();                                             /* lock; the bridge writes a whole frame between begin() and end() */
void end();                                               /* unlock, bumps display_rev if anything changed */

/* host side (any thread): the mpc_engine get_param / set_param of the platform call these first.  Return 0 if the key is not ours. */
int  get(const char* key, char* buf, int len);            /* "<key>", "<key>_display", "display_rev" */
bool set(const char* key, const char* val);               /* true if the key is a scene parameter (inputs are queued, display slots ignored) */

/* touch input: queued by set(), drained by the bridge */
struct Touch { int idx; int value; };
bool popTouch(Touch& t);
}
