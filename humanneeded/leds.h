#pragma once
#include "state.h"

void ledsBegin();
void ledsHello();             // start the "hello" spin (power-up, TEST hello)
void ledsSelftest();          // start the SELFTEST pattern: each LED R, G, B in turn
bool ledsSelftestActive();
// Draws one frame for the given state. dark: USB suspend, all off. sound: a sound is playing,
// so the LED current budget is lower.
void ledsRender(State state, bool dark, bool sound);
