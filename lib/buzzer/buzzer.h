#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    unsigned int gpio;
    unsigned int slice;
    bool pulse_active;
    uint32_t pulse_off_ms;
    bool speed_mode;
    bool speed_tone_enabled;
    uint32_t speed_pitch_hz;
    uint32_t speed_period_ms;
    uint32_t next_beep_ms;
    bool confirmation_active;
} buzzer_t;

// Initializes a passive PWM buzzer.  Call buzzer_update() from the main loop.
void buzzer_init(buzzer_t *buzzer, unsigned int gpio);

// Plays the first button press confirmation for 2 seconds.
void buzzer_enable_speed_mode(buzzer_t *buzzer, uint32_t now_ms);

// Compatibility entry point: speed warnings are disabled.
void buzzer_set_speed(buzzer_t *buzzer, float speed, uint32_t now_ms);

// Stops the confirmation when elapsed; never blocks.
void buzzer_update(buzzer_t *buzzer, uint32_t now_ms);

#endif
