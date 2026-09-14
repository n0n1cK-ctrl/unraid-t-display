#pragma once
#include <Arduino.h>
extern bool coverMode;
extern volatile uint8_t displayPage;
void coverSetup();
void coverLoop();
