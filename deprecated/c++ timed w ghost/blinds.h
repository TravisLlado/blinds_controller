////////////////////////////////////////////////////////////////////////////////
// Living Room Blinds Control Code, now without Alexa!
// written by Travis Llado
// last edited 2025-02-20
////////////////////////////////////////////////////////////////////////////////

#define PIN_STEPPER_DIRECTION 20U
#define PIN_STEPPER_STEP      19U
#define PIN_STEPPER_ENABLE_1  16U
#define PIN_STEPPER_ENABLE_2  21U
#define PIN_STEPPER_ENABLE_3  28U

#define TURNS_TO_OPEN     5.5F          // full rotations
#define DELAY_ENABLE      100U          // ms
#define DELAY_DISABLE     DELAY_ENABLE  // ms
#define DELAY_TICK        100U          // ms
#define STEPPER_TICK_HIGH 20U           // μs
#define STEPPER_TICK_LOW  200U          // μs
#define STEPPER_TICK_RAMP 5000U         // μs
#define STEPPER_TICK_STEP 100U          // μs/step

void blinds_init(void);
void blinds_open(void);
void blinds_close(void);

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
