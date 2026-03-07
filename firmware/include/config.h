/**
 * @file config.h
 * @brief OpenSmartFocuser — Hardware & Behaviour Configuration
 */

#pragma once

// --- Motor ---
#define STEPS_PER_REV     200
#define MICROSTEP_MODE    8

// --- Speed ---
#define MAX_SPEED         1000   // steps/sec
#define ACCELERATION      500    // steps/sec²

// --- Position Limits ---
#define MAX_POSITION      100000
#define MIN_POSITION      0
