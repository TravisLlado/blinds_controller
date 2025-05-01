////////////////////////////////////////////////////////////////////////////////

// blinds_controller
// v1
// for ESP32-C3
// authored by Travis Llado
// last edited 2025-03-21

////////////////////////////////////////////////////////////////////////////////

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

////////////////////////////////////////////////////////////////////////////////

#define PIN_LIMIT_SWITCH_DOWN   7
#define PIN_LIMIT_SWITCH_UP     6
#define PIN_BUTTON              9
#define PIN_MOTOR_SPEED         0
#define PIN_MOTOR_FORWARD       1
#define PIN_MOTOR_REVERSE       2
#define PIN_LED                 8

#define PWM_FREQUENCY   20000   // Hz
#define PWM_DUTY_CYCLE  95      // %
#define PWM_RESOLUTION  LEDC_TIMER_10_BIT

#define SLEEP_PERIOD_US     50000       // us
#define ACTIVE_PERIOD_US    1000        // us
#define MOVE_TIME_LIMIT_US  30000000    // us

////////////////////////////////////////////////////////////////////////////////

bool blinds_are_closed(void) {
    return gpio_get_level(PIN_LIMIT_SWITCH_DOWN);
}

bool blinds_are_open(void) {
    return gpio_get_level(PIN_LIMIT_SWITCH_UP);
}

void close_blinds(void) {
    int count = 0U;
    bool timeout = false;
    gpio_set_level(PIN_MOTOR_REVERSE, 1);
    while (!blinds_are_closed() && !timeout) {
        esp_rom_delay_us(ACTIVE_PERIOD_US);
        timeout = (count++ >= (MOVE_TIME_LIMIT_US / ACTIVE_PERIOD_US));
    }
    gpio_set_level(PIN_MOTOR_REVERSE, 0);
}

void open_blinds(void) {
    int count = 0U;
    bool timeout = false;
    gpio_set_level(PIN_MOTOR_FORWARD, 1);
    while (!blinds_are_open() && !timeout) {
        esp_rom_delay_us(ACTIVE_PERIOD_US);
        timeout = (count++ >= (MOVE_TIME_LIMIT_US / ACTIVE_PERIOD_US));
    }
    gpio_set_level(PIN_MOTOR_FORWARD, 0);
}

////////////////////////////////////////////////////////////////////////////////

bool button_is_pressed(void) {
    return !gpio_get_level(PIN_BUTTON);
}

////////////////////////////////////////////////////////////////////////////////

void led_off(void) {
    gpio_set_level(PIN_LED, 1);
}

void led_on(void) {
    gpio_set_level(PIN_LED, 0);
}

////////////////////////////////////////////////////////////////////////////////

void app_main(void) {
    gpio_config_t input_conf = {
        .pin_bit_mask = (1ULL << PIN_LIMIT_SWITCH_UP) | 
                        (1ULL << PIN_LIMIT_SWITCH_DOWN) | 
                        (1ULL << PIN_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&input_conf);

    gpio_config_t output_conf = {
        .pin_bit_mask = (1ULL << PIN_LED) |
                        (1ULL << PIN_MOTOR_FORWARD) |
                        (1ULL << PIN_MOTOR_REVERSE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&output_conf);

    // LEDC PWM timer configuration
    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = PWM_RESOLUTION,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = PWM_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));
    
    // LEDC PWM channel configuration
    ledc_channel_config_t channel_config = {
        .gpio_num = PIN_MOTOR_SPEED,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = (PWM_DUTY_CYCLE * (1 << PWM_RESOLUTION)) / 100,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
    
    while (1) {
        if (button_is_pressed()) {
            led_on();
            if (blinds_are_closed()) {
                open_blinds();
            } else {
                close_blinds();
            }
        }

        led_off();
        esp_rom_delay_us(SLEEP_PERIOD_US);
    }
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
