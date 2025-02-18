////////////////////////////////////////////////////////////////////////////////
// Living Room Blinds Control Code, now without Alexa!
// written by Travis Llado
// last edited 2025-02-15
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
#define STEPPER_TICK_RAMP 2000U         // μs
#define STEPPER_TICK_STEP 5U            // μs/step

enum BLINDS_POSITION_ENUM{
  OPEN,
  CLOSED
};

class blinds_motors{
public:
  blinds_motors(const bool blinds_are_open);
  void open(void);
  void close(void);
private:
  void turn(const bool direction);
  BLINDS_POSITION_ENUM position_{CLOSED};
};

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
