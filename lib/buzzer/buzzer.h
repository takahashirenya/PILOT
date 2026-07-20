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
    uint32_t last_speed_ms;
    uint32_t next_timeout_toggle_ms;
    bool timeout_alert_active;
    bool timeout_high_tone;
    bool confirmation_active;
} buzzer_t;

// Initializes a passive PWM buzzer.  Call buzzer_update() from the main loop.
void buzzer_init(buzzer_t *buzzer, unsigned int gpio, uint32_t now_ms);

// Stops the startup beep and enables speed-controlled tones permanently.
void buzzer_enable_speed_mode(buzzer_t *buzzer, uint32_t now_ms);

// Pass the newest CAN ID 4 speed value to update the tone immediately.
void buzzer_set_speed(buzzer_t *buzzer, float speed, uint32_t now_ms);

// Generates the scheduled pulse and stops an elapsed pulse; never blocks.
void buzzer_update(buzzer_t *buzzer, uint32_t now_ms);

#endif
