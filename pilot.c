#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "mcp2515.h"
#include "buzzer.h"

// Pin and CAN settings.
#define CAN_SPI spi0
#define CAN_SCK_PIN 2
#define CAN_MOSI_PIN 3
#define CAN_MISO_PIN 4
#define CAN_CS_PIN 9
#define CAN_INT_PIN 5
#define CAN_BITRATE MCP2515_BITRATE_1000KBPS
#define CAN_CLOCK MCP2515_CLOCK_8MHZ
#define CAN_SPI_BAUD (10 * 1000 * 1000)

#define CAN_ID_ELE 0x001
#define CAN_ID_LAD 0x002
#define CAN_ID_BUTTON 0x003
#define CAN_ID_SPEED 0x004
#define CAN_SEND_INTERVAL_MS 10
#define DEBUG_PRINT_INTERVAL_MS 100

#define ELE_ADC_GPIO 28
#define ELE_ADC_INPUT 2
#define LAD_ADC_GPIO 27
#define LAD_ADC_INPUT 1
#define BUTTON_GPIO 26
#define BUZZER_GPIO 14

#define ADC_HISTORY_SIZE 1
#define ADC_INPUT_COUNT 8

static uint16_t adc_history[ADC_INPUT_COUNT][ADC_HISTORY_SIZE] = {0};
static uint32_t adc_sum[ADC_INPUT_COUNT] = {0};
static uint8_t adc_index[ADC_INPUT_COUNT] = {0};
static uint8_t adc_count[ADC_INPUT_COUNT] = {0};

static void input_init(void) {
    gpio_init(BUTTON_GPIO);
    gpio_set_dir(BUTTON_GPIO, GPIO_IN);
    gpio_pull_up(BUTTON_GPIO);
}

static uint16_t servo_adc_read_avg(uint8_t adc_input) {
    adc_select_input(adc_input);
    uint16_t new_value = adc_read();
    if (adc_count[adc_input] < ADC_HISTORY_SIZE) {
        adc_history[adc_input][adc_index[adc_input]] = new_value;
        adc_sum[adc_input] += new_value;
        adc_count[adc_input]++;
    } else {
        adc_sum[adc_input] -= adc_history[adc_input][adc_index[adc_input]];
        adc_history[adc_input][adc_index[adc_input]] = new_value;
        adc_sum[adc_input] += new_value;
    }
    adc_index[adc_input] = (adc_index[adc_input] + 1) % ADC_HISTORY_SIZE;
    return (uint16_t)(adc_sum[adc_input] / adc_count[adc_input]);
}

static mcp2515_error_t can_send_adc(mcp2515_t *can, uint32_t id, uint16_t value) {
    can_frame_t frame = { .id = id, .dlc = 2,
        .data = { (uint8_t)(value & 0xff), (uint8_t)(value >> 8) } };
    return mcp2515_send_message(can, &frame);
}

static mcp2515_error_t can_send_button(mcp2515_t *can, bool pressed) {
    can_frame_t frame = { .id = CAN_ID_BUTTON, .dlc = 1, .data = { pressed ? 1 : 0 } };
    return mcp2515_send_message(can, &frame);
}

// ID 4: 4-byte IEEE-754 little-endian float, or 2-byte little-endian speed x100.
static bool can_decode_speed(const can_frame_t *frame, float *speed) {
    if (frame->dlc >= 4) {
        memcpy(speed, frame->data, sizeof(*speed));
        return *speed >= 0.0f && *speed <= 1000.0f;
    }
    if (frame->dlc == 2) {
        *speed = (float)((uint16_t)frame->data[0] | ((uint16_t)frame->data[1] << 8)) / 100.0f;
        return true;
    }
    return false;
}

int main(void) {
    stdio_init_all();
    sleep_ms(1500);
    input_init();
    adc_init();
    adc_gpio_init(ELE_ADC_GPIO);
    adc_gpio_init(LAD_ADC_GPIO);

    buzzer_t buzzer;
    buzzer_init(&buzzer, BUZZER_GPIO, to_ms_since_boot(get_absolute_time()));

    mcp2515_t can;
    mcp2515_init_struct(&can, CAN_SPI, CAN_SCK_PIN, CAN_MOSI_PIN, CAN_MISO_PIN,
                        CAN_CS_PIN, CAN_INT_PIN, CAN_SPI_BAUD);
    bool can_ready = mcp2515_begin(&can, CAN_BITRATE, CAN_CLOCK) == MCP2515_OK;

    bool button_was_pressed = false;
    bool button_latched = false;
    uint32_t last_debug_print_ms = 0;
    uint32_t next_send_ms = 0;

    while (true) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        bool button_pressed = gpio_get(BUTTON_GPIO) == 0;
        // Only the first press switches modes and emits the confirmation tone.
        // Later presses are still transmitted on CAN ID 3, but do not restart it.
        if (!buzzer.speed_mode && button_pressed && !button_was_pressed) {
            button_latched = true;
            buzzer_enable_speed_mode(&buzzer, now_ms);
        }
        button_was_pressed = button_pressed;

        // Receive first and drain both MCP2515 buffers, so the latest ID 4 is used.
        if (can_ready) {
            can_frame_t frame;
            while (mcp2515_check_receive(&can) && mcp2515_read_message(&can, &frame) == MCP2515_OK) {
                if (!frame.extended && !frame.rtr && frame.id == CAN_ID_SPEED) {
                    float speed;
                    if (can_decode_speed(&frame, &speed)) {
                        buzzer_set_speed(&buzzer, speed, now_ms);
                    }
                }
            }
        }

        // Existing input sender: unchanged IDs and 10 ms transmission interval.
        if (can_ready && (int32_t)(now_ms - next_send_ms) >= 0) {
            can_send_adc(&can, CAN_ID_ELE, servo_adc_read_avg(ELE_ADC_INPUT));
            can_send_adc(&can, CAN_ID_LAD, servo_adc_read_avg(LAD_ADC_INPUT));
            // Keep sending "pressed" after the first press, even after release.
            can_send_button(&can, button_latched);
            next_send_ms = now_ms + CAN_SEND_INTERVAL_MS;
        }

        buzzer_update(&buzzer, now_ms);
        if (now_ms - last_debug_print_ms >= DEBUG_PRINT_INTERVAL_MS) {
            printf("BUTTON raw=%u sent=%u speed_mode=%u\n", button_pressed,
                   button_latched, buzzer.speed_mode);
            last_debug_print_ms = now_ms;
        }
        sleep_ms(1);
    }
}
