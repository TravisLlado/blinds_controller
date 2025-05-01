#include "driver/gpio.h"
#include "esp_rom_sys.h"  // For delay function

#define LED_PIN GPIO_NUM_8  // Adjust for your board

extern "C" void app_main() {
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    while (1) {
        gpio_set_level(LED_PIN, 1);  // Turn LED on
        esp_rom_delay_us(500000);    // 500ms delay
        gpio_set_level(LED_PIN, 0);  // Turn LED off
        esp_rom_delay_us(500000);    // 500ms delay
    }
}
