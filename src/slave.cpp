/*
 * ESP32-C3 RS485 Slave - Hardware Window Blind Controller
 * Simple blocking implementation using real Blinds hardware class
 */

 // Libraries //////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include <HardwareSerial.h>
#include "blinds.h"
#include "button.h"
#include "config.h"

// Prototypes //////////////////////////////////////////////////////////////////

void buttonAction();
void sendPacket(const uint8_t cmd, const uint8_t data);
void executeCommand(const uint8_t cmd);

// Globals /////////////////////////////////////////////////////////////////////

HardwareSerial rs485_g(0);

Blinds blinds_g(
    PIN_PHASE_A,
    PIN_PHASE_B,
    PIN_PHASE_C,
    PIN_ENABLE,
    PIN_LIMIT,
    NUM_POLE_PAIRS,
    MOTION_LENGTH,
    MOTION_TIMEOUT,
    MOTOR_SPEED_MAX
);

Button button_g(PIN_BTN, BUTTON_PRESS_LENGTH, Button::ACTIVE_LOW, buttonAction);

// Helpers /////////////////////////////////////////////////////////////////////

void buttonAction() {
  Serial.println("Button pressed - toggling blind");
  blinds_g.toggle();
}

void executeCommand(const uint8_t cmd) {
  const bool blinds_are_closed{blinds_g.blindsAreClosed()};
  switch (cmd) {
    case CMD_QUERY:
      {
        sendPacket(RSP_STATE, !blinds_are_closed);
        Serial.printf("Slave %d: Query response - %s\n", SLAVE_ADDRESS, (blinds_are_closed ? "CLOSED" : "OPEN"));

      }
      break;
      
    case CMD_OPEN:
      {
        if (blinds_are_closed) {
          Serial.printf("Slave %d: Opening...\n", SLAVE_ADDRESS);
          blinds_g.open();
          sendPacket(RSP_COMPLETE, 1);
          Serial.printf("Slave %d: Open complete\n", SLAVE_ADDRESS);
        } else {
          sendPacket(RSP_COMPLETE, 1);
          Serial.printf("Slave %d: Already open\n", SLAVE_ADDRESS);
        }
      }
      break;
      
    case CMD_CLOSE:
      {
        if (blinds_are_closed) {
          sendPacket(RSP_COMPLETE, 0);
          Serial.printf("Slave %d: Already closed\n", SLAVE_ADDRESS);
        } else {
          Serial.printf("Slave %d: Closing...\n", SLAVE_ADDRESS);
          blinds_g.close();
          sendPacket(RSP_COMPLETE, 0);
          Serial.printf("Slave %d: Close complete\n", SLAVE_ADDRESS);
        }
      }
      break;
  }
}

void sendPacket(const uint8_t cmd, const uint8_t data) {
  digitalWrite(RS485_DE_RE, HIGH);
  delayMicroseconds(10);
  
  const uint8_t checksum = START_BYTE ^ SLAVE_ADDRESS ^ cmd ^ data;
  
  rs485_g.write(START_BYTE);
  rs485_g.write(SLAVE_ADDRESS);
  rs485_g.write(cmd);
  rs485_g.write(data);
  rs485_g.write(checksum);
  rs485_g.write(END_BYTE);
  
  rs485_g.flush();
  delayMicroseconds(10);
  digitalWrite(RS485_DE_RE, LOW);
}

// Setup ///////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);
  Serial.printf("Slave %d starting...\n", SLAVE_ADDRESS);
  
  button_g.begin();
  
  pinMode(RS485_DE_RE, OUTPUT);
  digitalWrite(RS485_DE_RE, LOW);
  
  rs485_g.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
  
  Serial.println("Ready.");
}

// Loop ////////////////////////////////////////////////////////////////////////

void loop() {
  button_g.update();
  
  // Process RS485 incoming
  static uint8_t rxBuffer[5];
  static uint8_t rxIndex{0};
  
  while (rs485_g.available()) {
    const uint8_t b = rs485_g.read();
    
    if ((rxIndex == 0) && (b != START_BYTE)) continue;
    
    rxBuffer[rxIndex++] = b;
    
    if (rxIndex == 5) {
      if ((rxBuffer[4] == END_BYTE) && (rxBuffer[1] == SLAVE_ADDRESS)) {
        const uint8_t cmd{rxBuffer[2]};
        const uint8_t checksum = START_BYTE ^ SLAVE_ADDRESS ^ cmd;
        
        if (rxBuffer[3] == checksum) {
          executeCommand(cmd);
        }
      }
      rxIndex = 0;
    }
  }
  
  delay(10);
}

// End of file /////////////////////////////////////////////////////////////////