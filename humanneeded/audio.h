#pragma once
#include <Arduino.h>

// Sound playback. With ENABLE_AUDIO 0 nothing is played, but plays are still
// counted so they can be checked over serial (STATUS).

void audioBegin();
void audioTick();  // feeds the audio (I2S or PWM) buffer; call every loop

// sound is a Sound value; volume 0-100 is scaled by the global volume; repeat 1-5.
void audioPlay(uint8_t sound, uint8_t volume, uint8_t repeat);

void audioMute(uint32_t minutes);  // 0 = unmute
bool audioMuted();

uint32_t audioPlayCount();
uint8_t audioLastSound();
