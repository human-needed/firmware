#pragma once
#include "state.h"

#define EV_END ST_COUNT  // pseudo-state for "EV end": forget the session

// Clock for session logic. Equals millis() unless the SKIP test command moved it forward.
uint32_t nowMs();
void skipMinutes(uint32_t minutes);

// Applies "EV <state> <id> [agent]". state is a State or EV_END.
void sessionsEvent(int state, const char *id, const char *agent);

// Runs stale guards, done timeout and the needs reminder. Call often.
void sessionsTick();

void sessionsClear();

// Highest-priority state among all live sessions (idle if none).
State sessionsTop();

// {"id":..,"agent":..,"state":..,"age_s":..} entries, comma separated.
String sessionsJson();
int sessionsCount();
