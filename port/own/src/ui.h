/* ui: the screen writer.  Controls are addressed by name ("patternEditor.label[0]", "g.titleBar.titleLabel"); one frame is built with
 * ui::begin() ... ui::end(), which writes only what changed into the scene slots (xxl_scene).  Slot encodings are those of the skin
 * generator (port/skin/gen/generic.py): the control's screen index is part of every state, so one pool slot serves many screens. */
#pragma once
#include <stdint.h>

namespace ui
{
void init();                                   /* after xxl_scene::init() */
int row(const char* name);                     /* index of a control, -1 if the skin has none (logged once) */
int module(int row);                           /* screen index of a control */
int rows(int row);                             /* list capacity of a list control */
int places(int row);
const char* ctlName(int row);                  /* name of a control by row */

enum Line { LN_NONE = 0, LN_RED = 1, LN_GREEN = 2, LN_DARK = 3 };
void begin();
/* a text control: name text, optional value text on a second line (bottom buttons), colour variants, underline */
void label(int r, const char* a, int va = 0, const char* b = nullptr, int vb = 0, int line = 0);
void dots(int r, int count, int current);      /* the page dots of a bottom button (2 or 3 pages) */
void bar(int r, int pct, bool disabled = false);          /* 0..100 */
void barSigned(int r, int v, int range);       /* -range..range around the middle */
void list(int r, int n, const char* const* names, const char* const* vals, int sel, int top, const bool* detail = nullptr);
void bg(int r);
void frame(int r, int place);
void end();

/* touch zones the skin reports: list rows tapped (value = row + 1) */
struct ListTap { int row; int index; };        /* control row, row index inside the list */
bool popListTap(ListTap& t);
bool popOtherTap(int& idx, int& value);          /* pattern grid zones (phRR) and anything else */
}
