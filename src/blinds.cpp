// blinds.cpp //////////////////////////////////////////////////////////////////

#include "blinds.h"

// Constants ///////////////////////////////////////////////////////////////////

constexpr ledc_timer_t   LEDC_TIMER   = LEDC_TIMER_0;
constexpr uint32_t       PWM_FREQ_HZ  = 20000U;
constexpr uint8_t        PWM_RES_BITS = 8;

// Public //////////////////////////////////////////////////////////////////////

Blinds::Blinds(gpio_num_t dirPin, gpio_num_t spdPin, gpio_num_t encPin,
    gpio_num_t limPin, uint32_t openTicks, uint32_t timeoutMs, 
    float dutyCycle, ledc_channel_t pwmChannel)
    : dir_pin(dirPin), spd_pin(spdPin), enc_pin(encPin), lim_pin(limPin),
      open_ticks(openTicks), timeout_us(timeoutMs * 1000U),
      duty_pwm(static_cast<uint32_t>(dutyCycle * ((1 << PWM_RES_BITS) - 1))),
      ledc_channel(pwmChannel),
      tick_count(0U) {

  // Configure GPIOs
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << dir_pin) | (1ULL << spd_pin);
  gpio_config(&io_conf);

  io_conf.intr_type = GPIO_INTR_POSEDGE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  io_conf.pin_bit_mask = (1ULL << enc_pin);
  gpio_config(&io_conf);

  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.pin_bit_mask = (1ULL << lim_pin);
  gpio_config(&io_conf);

  // PWM Setup (shared timer)
  static bool pwm_timer_initialized = false;
  if (!pwm_timer_initialized) {
    ledc_timer_config_t ledc_timer = {
      .speed_mode       = LEDC_LOW_SPEED_MODE,
      .duty_resolution  = static_cast<ledc_timer_bit_t>(PWM_RES_BITS),
      .timer_num        = LEDC_TIMER,
      .freq_hz          = PWM_FREQ_HZ,
      .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);
    pwm_timer_initialized = true;
  }

  ledc_channel_config_t pwm_config = {
    .gpio_num       = spd_pin,
    .speed_mode     = LEDC_LOW_SPEED_MODE,
    .channel        = ledc_channel,
    .intr_type      = LEDC_INTR_DISABLE,
    .timer_sel      = LEDC_TIMER,
    .duty           = 0,
    .hpoint         = 0,
    .flags          = {}
  };
  ledc_channel_config(&pwm_config);
  stopMotor();

// Interrupt setup (only install ISR service once)
// static bool isr_service_installed = false;
// if (!isr_service_installed) {
  gpio_install_isr_service(0);  // pass 0 for default ISR service
//   isr_service_installed = true;
// }
gpio_set_intr_type(enc_pin, GPIO_INTR_POSEDGE);
gpio_isr_handler_add(enc_pin, isrHandler, this);
}

void Blinds::open() {
  if (blindsAreClosed()) {
    tick_count = 0;
    const int64_t start = esp_timer_get_time();
    runMotor(Direction::OPEN);

    while ((tick_count < open_ticks) && ((esp_timer_get_time() - start) < timeout_us)) {
      // wait
    }
    stopMotor();
  }
}

void Blinds::close() {
  if (!blindsAreClosed()) {
    const int64_t start = esp_timer_get_time();
    runMotor(Direction::CLOSE);

    while (!blindsAreClosed() && ((esp_timer_get_time() - start) < timeout_us)) {
      // wait
    }
    stopMotor();
  }
}
  
void Blinds::toggle() {
  if (blindsAreClosed()) {
    open();
  } else {
    close();
  }
}

bool Blinds::blindsAreClosed() const {
  return gpio_get_level(lim_pin) == LimitSwitchState::PRESSED;
}

// Private /////////////////////////////////////////////////////////////////////

void IRAM_ATTR Blinds::isrHandler(void* arg) {
  static_cast<Blinds*>(arg)->tick_count++;
}
  
void Blinds::runMotor(const Direction dir) {
  gpio_set_level(dir_pin, dir);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, ledc_channel, duty_pwm);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, ledc_channel);
}
  
void Blinds::stopMotor() {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, ledc_channel, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, ledc_channel);
}

// End of file /////////////////////////////////////////////////////////////////
