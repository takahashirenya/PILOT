#include "buzzer.h"

#include "hardware/gpio.h"
#include "hardware/pwm.h"

#define PITCH_HIGH_HZ      6000u
#define PITCH_LOW_HZ       1000u
#define PITCH_STARTUP_HZ    4000u
#define PITCH_CONFIRM_HZ    6000u
#define FAST_HIGH           12.0f
#define FAST_LOW            11.5f
#define SLOW_HIGH            9.5f
#define SLOW_LOW             6.0f
#define PERIOD_MIN_MS        100u
#define PERIOD_MAX_MS        500u
#define PULSE_MS             100u
#define CONFIRM_PULSE_MS    1000u
#define STARTUP_PERIOD_MS   1000u
#define SPEED_TIMEOUT_MS    5000u
#define TIMEOUT_TONE_MS      500u

static void buzzer_off(buzzer_t *buzzer) {
    pwm_set_gpio_level(buzzer->gpio, 0);
    buzzer->pulse_active = false;
}

static void buzzer_on(buzzer_t *buzzer, uint32_t pitch_hz, uint32_t now_ms) {
    // PWM clock: 125 MHz / 4.  The buzzer is driven at a 50 % duty cycle.
    uint32_t wrap = (125000000u / 4u) / pitch_hz - 1u;
    pwm_set_wrap(buzzer->slice, (uint16_t)wrap);
    pwm_set_gpio_level(buzzer->gpio, wrap / 2u);
    buzzer->pulse_active = true;
    buzzer->pulse_off_ms = now_ms + PULSE_MS;
}

static void buzzer_on_continuous(buzzer_t *buzzer, uint32_t pitch_hz) {
    uint32_t wrap = (125000000u / 4u) / pitch_hz - 1u;
    pwm_set_wrap(buzzer->slice, (uint16_t)wrap);
    pwm_set_gpio_level(buzzer->gpio, wrap / 2u);
    buzzer->pulse_active = false;
}

static bool speed_to_tone(float speed, uint32_t *pitch_hz, uint32_t *period_ms) {
    if (speed > FAST_LOW) {
        *pitch_hz = PITCH_HIGH_HZ;
        if (speed >= FAST_HIGH) {
            *period_ms = PERIOD_MIN_MS;
        } else {
            float v = (speed - FAST_LOW) / (FAST_HIGH - FAST_LOW);
            *period_ms = (uint32_t)((PERIOD_MAX_MS - PERIOD_MIN_MS) * (1.0f - v) + PERIOD_MIN_MS);
        }
        return true;
    }

    if (speed > SLOW_LOW && speed < SLOW_HIGH) {
        *pitch_hz = PITCH_LOW_HZ;
        float v = (speed - SLOW_LOW) / (SLOW_HIGH - SLOW_LOW);
        *period_ms = (uint32_t)((PERIOD_MAX_MS - PERIOD_MIN_MS) * v + PERIOD_MIN_MS);
        return true;
    }

    return false; // <= 6.0 and 9.5 through 11.5 are silent.
}

void buzzer_init(buzzer_t *buzzer, unsigned int gpio, uint32_t now_ms) {
    *buzzer = (buzzer_t){ .gpio = gpio, .last_speed_ms = now_ms };
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    buzzer->slice = pwm_gpio_to_slice_num(gpio);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 4.0f);
    pwm_init(buzzer->slice, &config, true);
    buzzer_off(buzzer);
}

void buzzer_enable_speed_mode(buzzer_t *buzzer, uint32_t now_ms) {
    buzzer->speed_mode = true;
    buzzer->speed_tone_enabled = false;
    buzzer->timeout_alert_active = false;
    buzzer->confirmation_active = true;
    // This confirmation has priority over every other buzzer sound.
    buzzer_on(buzzer, PITCH_CONFIRM_HZ, now_ms);
    buzzer->pulse_off_ms = now_ms + CONFIRM_PULSE_MS;
    buzzer->next_beep_ms = buzzer->pulse_off_ms;
}

void buzzer_set_speed(buzzer_t *buzzer, float speed, uint32_t now_ms) {
    buzzer->last_speed_ms = now_ms;
    if (buzzer->timeout_alert_active) {
        buzzer->timeout_alert_active = false;
        if (!buzzer->confirmation_active) {
            buzzer_off(buzzer);
        }
        buzzer->next_beep_ms = now_ms;
    }

    if (!buzzer->speed_mode) {
        return;
    }

    buzzer->speed_tone_enabled = speed_to_tone(speed, &buzzer->speed_pitch_hz,
                                                &buzzer->speed_period_ms);
    if (buzzer->speed_tone_enabled) {
        // Do not cut off the button-press confirmation. Otherwise apply the
        // newest CAN value without waiting for an old speed interval.
        buzzer->next_beep_ms = buzzer->pulse_active ? buzzer->pulse_off_ms : now_ms;
    } else if (!buzzer->confirmation_active) {
        buzzer_off(buzzer);
    }
}

void buzzer_update(buzzer_t *buzzer, uint32_t now_ms) {
    if (buzzer->confirmation_active) {
        if ((int32_t)(now_ms - buzzer->pulse_off_ms) < 0) {
            return;
        }
        buzzer->confirmation_active = false;
        buzzer_off(buzzer);
        buzzer->next_beep_ms = now_ms;
    }

    bool speed_timed_out = (uint32_t)(now_ms - buzzer->last_speed_ms) >= SPEED_TIMEOUT_MS;
    if (speed_timed_out) {
        if (!buzzer->timeout_alert_active) {
            buzzer->timeout_alert_active = true;
            buzzer->timeout_high_tone = true;
            buzzer_on_continuous(buzzer, PITCH_HIGH_HZ);
            buzzer->next_timeout_toggle_ms = now_ms + TIMEOUT_TONE_MS;
        } else if ((int32_t)(now_ms - buzzer->next_timeout_toggle_ms) >= 0) {
            buzzer->timeout_high_tone = !buzzer->timeout_high_tone;
            buzzer_on_continuous(buzzer, buzzer->timeout_high_tone
                                             ? PITCH_HIGH_HZ : PITCH_LOW_HZ);
            buzzer->next_timeout_toggle_ms = now_ms + TIMEOUT_TONE_MS;
        }
        return;
    }

    if (buzzer->pulse_active && (int32_t)(now_ms - buzzer->pulse_off_ms) >= 0) {
        buzzer_off(buzzer);
    }

    if (buzzer->speed_mode) {
        if (buzzer->speed_tone_enabled && (int32_t)(now_ms - buzzer->next_beep_ms) >= 0) {
            buzzer_on(buzzer, buzzer->speed_pitch_hz, now_ms);
            buzzer->next_beep_ms = now_ms + buzzer->speed_period_ms;
        }
    } else if ((int32_t)(now_ms - buzzer->next_beep_ms) >= 0) {
        buzzer_on(buzzer, PITCH_STARTUP_HZ, now_ms);
        buzzer->next_beep_ms = now_ms + STARTUP_PERIOD_MS;
    }
}
