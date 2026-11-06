#pragma once
#include "state.h"

void ledsBegin();
void ledsHello();             // start the boot "hello" spin
void ledsRender(State state); // draw one frame for the given display state
