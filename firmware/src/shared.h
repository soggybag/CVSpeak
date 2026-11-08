#pragma once
// State shared between the cores.
//   core 0: controls, UI and display; publishes Controls about every 1 ms.
//   core 1: audio; reads Controls once per block and reports Status back.

#include <cstdint>

#include "cvspeak/sequencer.h"
#include "hardware/sync.h"

struct Controls {
    float pitch = -1.f;       // volts, as VoiceInputs::pitch (knob + V/OCT)
    float speed = 1.f;
    float intonation = 1.f;
    float chirp = 0.5f;
    float formant = 0.f;
    float bend = 0.f;
    float blend = 1.f;
    bool carrierPatched = false;
    cvspeak::Mode mode = cvspeak::Mode::Word;
    int selected = 0;         // word or phrase, after SEL CV
};

struct Status {
    volatile bool speaking = false;
    volatile int currentWord = -1;
    volatile int seqPhrase = -1;
    volatile int spokenPos = -1;
    volatile int nextPos = 0;
    volatile uint32_t loadPermille = 0;  // core 1 busy time, last second
};

struct Shared {
    Controls buffer[2];
    volatile uint32_t current = 0;
    volatile uint32_t speakPresses = 0;  // SPEAK button, counted by core 0
    Status status;

    void publish(const Controls& c) {
        const uint32_t next = current ^ 1u;
        buffer[next] = c;
        __dmb();
        current = next;
    }
    Controls read() const { return buffer[current]; }
};

extern Shared g_shared;
