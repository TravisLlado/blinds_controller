#include <cmath>
#include <esp_timer.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PWM_GPIO       GPIO_NUM_5
#define DIR_GPIO       GPIO_NUM_6
#define LED_GPIO       GPIO_NUM_8

#define PWM_FREQ_HZ    20000
#define PWM_RES_BITS   LEDC_TIMER_10_BIT
#define PWM_MAX_DUTY   ((1 << PWM_RES_BITS) - 1)

#define UPDATE_MS      10
#define SINE_PERIOD_MS 2000  // full sine wave every 10 sec

// Duty range: 20% to 60% (0.2 to 0.6)
constexpr float DUTY_MIN = 0.55F;
constexpr float DUTY_MAX = 0.70F;
constexpr float DUTY_CENTER = (DUTY_MIN + DUTY_MAX) / 2.0F;
constexpr float DUTY_SPAN   = (DUTY_MAX - DUTY_MIN) / 2.0F;

extern "C" void app_main() {
    // Configure direction pin (fixed forward)
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << DIR_GPIO);
    gpio_config(&io_conf);
    gpio_set_level(DIR_GPIO, 1); // constant direction

    // Configure LEDC timer
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.duty_resolution = PWM_RES_BITS;
    ledc_timer.freq_hz = PWM_FREQ_HZ;
    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_timer.timer_num = LEDC_TIMER_0;
    ledc_timer_config(&ledc_timer);

    // Motor PWM (channel 0)
    ledc_channel_config_t motor_channel = {};
    motor_channel.channel    = LEDC_CHANNEL_0;
    motor_channel.duty       = 0;
    motor_channel.gpio_num   = PWM_GPIO;
    motor_channel.speed_mode = LEDC_LOW_SPEED_MODE;
    motor_channel.hpoint     = 0;
    motor_channel.timer_sel  = LEDC_TIMER_0;
    ledc_channel_config(&motor_channel);

    // LED PWM (channel 1, inverted logic)
    ledc_channel_config_t led_channel = {};
    led_channel.channel    = LEDC_CHANNEL_1;
    led_channel.duty       = PWM_MAX_DUTY;
    led_channel.gpio_num   = LED_GPIO;
    led_channel.speed_mode = LEDC_LOW_SPEED_MODE;
    led_channel.hpoint     = 0;
    led_channel.timer_sel  = LEDC_TIMER_0;
    ledc_channel_config(&led_channel);

    int64_t start_time = esp_timer_get_time();

    while (true) {
        int64_t now_us = esp_timer_get_time();
        int64_t elapsed_ms = (now_us - start_time) / 1000;

        // Sine wave in [-1, 1]
        float phase = (2.0f * M_PI * elapsed_ms) / SINE_PERIOD_MS;
        float sine_val = sinf(phase);

        // Rescale sine wave to [DUTY_MIN, DUTY_MAX]
        float scaled = DUTY_CENTER + DUTY_SPAN * sine_val;
        uint32_t motor_duty = static_cast<uint32_t>(scaled * PWM_MAX_DUTY);
        uint32_t led_duty = PWM_MAX_DUTY - motor_duty;

        // Apply to motor and LED
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, motor_duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, led_duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);

        vTaskDelay(pdMS_TO_TICKS(UPDATE_MS));
    }
}
