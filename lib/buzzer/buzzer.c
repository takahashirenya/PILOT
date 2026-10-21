#include "buzzer.h"

#include "hardware/gpio.h"
#include "hardware/pwm.h"

#define PITCH_CONFIRM_HZ 6000u
#define CONFIRM_PULSE_MS 2000u

static void buzzer_off(buzzer_t *buzzer) {
    pwm_set_gpio_level(buzzer->gpio, 0);
    buzzer->pulse_active = false;
}

void buzzer_init(buzzer_t *buzzer, unsigned int gpio) {
    *buzzer = (buzzer_t){ .gpio = gpio };
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    buzzer->slice = pwm_gpio_to_slice_num(gpio);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 4.0f);
    pwm_init(buzzer->slice, &config, true);
    buzzer_off(buzzer);
}

void buzzer_enable_speed_mode(buzzer_t *buzzer, uint32_t now_ms) {
    if (buzzer->speed_mode) {
        return;
    }
    buzzer->speed_mode = true;
    buzzer->confirmation_active = true;
    uint32_t wrap = (125000000u / 4u) / PITCH_CONFIRM_HZ - 1u;
    pwm_set_wrap(buzzer->slice, (uint16_t)wrap);
    pwm_set_gpio_level(buzzer->gpio, wrap / 2u);
    buzzer->pulse_active = true;
    buzzer->pulse_off_ms = now_ms + CONFIRM_PULSE_MS;
}

void buzzer_set_speed(buzzer_t *buzzer, float speed, uint32_t now_ms) {
    // Speed warnings are disabled and cannot interrupt confirmation.
    (void)buzzer;
    (void)speed;
    (void)now_ms;
}

void buzzer_update(buzzer_t *buzzer, uint32_t now_ms) {
    if (buzzer->confirmation_active &&
        (int32_t)(now_ms - buzzer->pulse_off_ms) >= 0) {
        buzzer->confirmation_active = false;
        buzzer_off(buzzer);
    }
}
