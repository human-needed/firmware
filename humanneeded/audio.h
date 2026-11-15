#pragma once
#include <Arduino.h>

// Sound playback. The amp is only switched on while a sound plays. With ENABLE_AUDIO 0
// nothing is played, but plays are still counted so they can be checked over serial (STATUS).

void audioBegin();
void audioTick();  // feeds the audio (I2S or PWM) buffer and switches the amp; call every loop

// sound is a Sound value; volume 0-100 is scaled by the global volume; repeat 1-5.
void audioPlay(uint8_t sound, uint8_t volume, uint8_t repeat);
void audioSweep();          // SELFTEST speaker sweep (ignores mute and volume settings)
void audioStop();
bool audioPlaying();        // a sound plays or the amp is still on
bool audioAmpOn();
void audioSuspend(bool on); // USB suspend: stop, amp off, play nothing until resumed

void audioMute(uint32_t minutes);  // 0 = unmute, else timed
void audioMuteOn();                // until turned off
bool audioMuted();
void audioMuteText(char *out, size_t n);  // "off", "on" or minutes left

uint32_t audioPlayCount();
uint8_t audioLastSound();
