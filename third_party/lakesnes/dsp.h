
#ifndef DSP_H
#define DSP_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Dsp Dsp;

#include "apu.h"
#include "statehandler.h"

typedef struct DspChannel {
  // pitch
  uint16_t pitch;
  uint16_t pitchCounter;
  bool pitchModulation;
  // brr decoding
  int16_t decodeBuffer[12];
  uint8_t bufferOffset;
  uint8_t srcn;
  uint16_t decodeOffset;
  uint8_t blockOffset; // offset within brr block
  uint8_t brrHeader;
  bool useNoise;
  uint8_t startDelay;
  // adsr, envelope, gain
  uint8_t adsrRates[4]; // attack, decay, sustain, gain
  uint8_t adsrState; // 0: attack, 1: decay, 2: sustain, 3: release
  uint8_t sustainLevel;
  uint8_t gainSustainLevel;
  bool useGain;
  uint8_t gainMode;
  bool directGain;
  uint16_t gainValue; // for direct gain
  uint16_t preclampGain; // for bent increase
  uint16_t gain;
  // keyon/off
  bool keyOn;
  bool keyOff;
  // output
  int16_t sampleOut; // final sample, to be multiplied by channel volume
  int8_t volumeL;
  int8_t volumeR;
  bool echoEnable;
} DspChannel;

struct Dsp {
  Apu* apu;
  // mirror ram
  uint8_t ram[0x80];
  // 8 channels
  DspChannel channel[8];
  // overarching
  uint16_t counter;
  uint16_t dirPage;
  bool evenCycle;
  bool mute;
  bool reset;
  int8_t masterVolumeL;
  int8_t masterVolumeR;
  // accumulation
  int16_t sampleOutL;
  int16_t sampleOutR;
  int16_t echoOutL;
  int16_t echoOutR;
  // noise
  int16_t noiseSample;
  uint8_t noiseRate;
  // echo
  bool echoWrites;
  int8_t echoVolumeL;
  int8_t echoVolumeR;
  int8_t feedbackVolume;
  uint16_t echoBufferAdr;
  uint16_t echoDelay;
  uint16_t echoLength;
  uint16_t echoBufferIndex;
  uint8_t firBufferIndex;
  int8_t firValues[8];
  int16_t firBufferL[8];
  int16_t firBufferR[8];
  // sample ring buffer (1024 samples, *2 for stereo)
  int16_t sampleBuffer[0x400 * 2];
  uint16_t sampleOffset; // current offset in samplebuffer
  // topgear-pc: voices left out of what is heard (outputMask bit n: voice n). Presentation
  // only: every voice, the echo buffer in APU RAM and every register are computed exactly as
  // before; only sampleBuffer (the output) changes. The kept voices' echo comes from a
  // shadow echo buffer outside APU RAM, fed by those voices alone. Not part of the state.
  uint8_t outputMask;
  int shadowOutL, shadowOutR, shadowEchoInL, shadowEchoInR;
  int16_t shadowFirL[8], shadowFirR[8];
  int16_t shadowEcho[0x7800 / 2]; // the longest echo: 15 * 2048 bytes
};

Dsp* dsp_init(Apu* apu);
void dsp_free(Dsp* dsp);
void dsp_reset(Dsp* dsp);
void dsp_handleState(Dsp* dsp, StateHandler* sh);
void dsp_cycle(Dsp* dsp);
uint8_t dsp_read(Dsp* dsp, uint8_t adr);
void dsp_write(Dsp* dsp, uint8_t adr, uint8_t val);
void dsp_getSamples(Dsp* dsp, int16_t* sampleData, int samplesPerFrame);
// topgear-pc: see outputMask.
void dsp_setOutputMask(Dsp* dsp, uint8_t mask);

#endif
