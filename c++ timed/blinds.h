// Stepper Motor Hardware Config
#define PIN_STEPPER_DIRECTION 20U
#define PIN_STEPPER_STEP      19U
#define PIN_STEPPER_ENABLE_1  16U
#define PIN_STEPPER_ENABLE_2  21U
#define PIN_STEPPER_ENABLE_3  28U

enum BLINDS_POSITION_ENUM{
  OPEN,
  CLOSED
};

class blinds{
public:
  blinds(const bool blinds_are_open);
  void open(void);
  void close(void);
private:
  void turn(const bool direction);
  BLINDS_POSITION_ENUM position{CLOSED};
};



