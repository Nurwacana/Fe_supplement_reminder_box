#pragma once
#include <Arduino.h>

typedef struct {
    uint16_t freq;       // Hz (0 = hening/rest)
    uint16_t play_ms;    // Durasi nada berbunyi
    uint16_t pause_ms;   // Delay diam setelah nada selesai
} RingtoneStep;

// Motif Samsung "Over the Horizon" (Frekuensi Asli)
const RingtoneStep samsung_ringtone[] = {
    {  831, 180,   20 },
    {  932, 180,   20 },
    { 1046, 180,   20 },
    { 1245, 360,   40 },
    { 1046, 180,   20 },
    {  932, 600, 1200 }
};

// Motif Samsung "Over the Horizon" (Resonansi Tinggi / Oktaf +1, Optimal untuk Buzzer Piezo)
const RingtoneStep samsung_ringtone_resonant[] = {
    { 1662, 180,   20 },
    { 1864, 180,   20 },
    { 2092, 180,   20 },
    { 2490, 360,   40 },
    { 2092, 180,   20 },
    { 1864, 600, 1200 }
};

// Melodi Nokia (Frekuensi Asli)
const RingtoneStep nokia_ringtone[] = {
    { 1319, 140,   20 },
    { 1175, 140,   20 },
    {  740, 280,   30 },
    {  831, 280,   30 },
    { 1109, 140,   20 },
    {  988, 140,   20 },
    {  587, 280,   30 },
    {  659, 280,   30 },
    {  988, 140,   20 },
    {  880, 140,   20 },
    {  554, 280,   30 },
    {  659, 280,   30 },
    {  880, 550, 1500 }
};

// Melodi Nokia (Resonansi Tinggi / Oktaf +1, Optimal untuk Buzzer Piezo)
const RingtoneStep nokia_ringtone_resonant[] = {
    { 2638, 140,   20 },
    { 2350, 140,   20 },
    { 1480, 280,   30 },
    { 1662, 280,   30 },
    { 2218, 140,   20 },
    { 1976, 140,   20 },
    { 1174, 280,   30 },
    { 1318, 280,   30 },
    { 1976, 140,   20 },
    { 1760, 140,   20 },
    { 1108, 280,   30 },
    { 1318, 280,   30 },
    { 1760, 550, 1500 }
};

const size_t SAMSUNG_STEPS = sizeof(samsung_ringtone) / sizeof(samsung_ringtone[0]);
const size_t NOKIA_STEPS   = sizeof(nokia_ringtone)   / sizeof(nokia_ringtone[0]);

enum RingtoneChoice {
    RT_SAMSUNG_RESONANT = 0, // Default: Samsung Over The Horizon nada tinggi (paling nyaring di buzzer)
    RT_SAMSUNG_ORIGINAL = 1, // Samsung nada asli
    RT_NOKIA_RESONANT   = 2, // Nokia nada tinggi (paling nyaring di buzzer)
    RT_NOKIA_ORIGINAL   = 3  // Nokia nada asli
};
