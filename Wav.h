#include <Arduino.h>

// 16bit, monoral, 16000Hz,  linear PCM
void CreateWavHeader(byte* header, int waveDataSize);  // size of header is 44
