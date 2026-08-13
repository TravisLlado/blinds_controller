/*
 * ESP32-C3 RS485 Slave - Hardware Window Blind Controller
 * Simple blocking implementation using real Blinds hardware class
 */

 // Libraries //////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp_task_wdt.h>
#include "blinds.h"
#include "button.h"
#include "config.h"

// Prototypes //////////////////////////////////////////////////////////////////

void buttonAction();
void sendPacket(const uint8_t cmd, const uint8_t data);
void executeCommand(const uint8_t cmd);
void handleConfigCommand(const uint8_t* buffer);

// Globals /////////////////////////////////////////////////////////////////////

HardwareSerial rs485_g(0);

Blinds blinds_g(
    PIN_PHASE_A,
    PIN_PHASE_B,
    PIN_PHASE_C,
    PIN_ENABLE,
    PIN_LIMIT,
    NUM_POLE_PAIRS,
    MOTION_LENGTH,       // Default value, will be overwritten by master
    MOTION_TIMEOUT,      // Default value, will be recalculated when config is received
    MOTOR_SPEED_OPEN,    // Default value, will be overwritten by master
    MOTOR_SPEED_CLOSE    // Default value, will be overwritten by master
);

Button button_g(PIN_BTN, BUTTON_DEBOUNCE_MS, BUTTON_SHORT_PRESS_MS, BUTTON_LONG_PRESS_MS,
                Button::ACTIVE_LOW, buttonAction, nullptr);

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
        sendPacket(RSP_COMMAND_ACK, 1);
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
        sendPacket(RSP_COMMAND_ACK, 0);
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

    case CMD_CLOSE_HALF:
      {
        if (blinds_are_closed) {
          sendPacket(RSP_COMPLETE, 0);
          Serial.printf("Slave %d: Already closed, skip close-half\n", SLAVE_ADDRESS);
        } else {
          Serial.printf("Slave %d: Close halfway...\n", SLAVE_ADDRESS);
          blinds_g.closeHalfway();
          sendPacket(RSP_COMPLETE, 0);
          Serial.printf("Slave %d: Close-half complete\n", SLAVE_ADDRESS);
        }
      }
      break;
  }
}

void handleConfigCommand(const uint8_t* buffer) {
  // Extract floats from buffer (bytes 3-14)
  ConfigPacket config;
  memcpy(&config.motion_length, &buffer[3], sizeof(float));
  memcpy(&config.motor_speed_open, &buffer[7], sizeof(float));
  memcpy(&config.motor_speed_close, &buffer[11], sizeof(float));
  
  // Apply configuration to blinds (timeout is automatically recalculated)
  blinds_g.setMotionLength(config.motion_length);
  blinds_g.setMotorSpeedOpen(config.motor_speed_open);
  blinds_g.setMotorSpeedClose(config.motor_speed_close);
  
  Serial.printf("Slave %d: Config received\n", SLAVE_ADDRESS);
  Serial.printf("  Motion Length: %.2f turns\n", config.motion_length);
  Serial.printf("  Motor Speed (Open): %.2f RPM\n", config.motor_speed_open);
  Serial.printf("  Motor Speed (Close): %.2f RPM\n", config.motor_speed_close);
  Serial.printf("  Motion Timeout: %.1f seconds\n", blinds_g.getMotionTimeout());
  
  // Send acknowledgment
  sendPacket(RSP_CONFIG_ACK, 1);
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

  esp_task_wdt_init(WATCHDOG_TIMEOUT_SEC, true);
  esp_task_wdt_add(NULL);

  Serial.println("Ready. Waiting for configuration from master...");
}

// Loop ////////////////////////////////////////////////////////////////////////

void loop() {
  esp_task_wdt_reset();
  button_g.update();

  // Process RS485 incoming
  static uint8_t rxBuffer[17];  // Maximum size for config packet (now 17 bytes)
  static uint8_t rxIndex{0};
  static uint8_t expectedLength{5};  // Default for standard commands
  
  while (rs485_g.available()) {
    const uint8_t b = rs485_g.read();
    
    if ((rxIndex == 0) && (b != START_BYTE)) continue;
    
    rxBuffer[rxIndex++] = b;
    
    // After receiving address and command, determine expected packet length
    if (rxIndex == 3) {
      if (rxBuffer[2] == CMD_SET_CONFIG) {
        expectedLength = 17;  // Config packets are now 17 bytes
      } else {
        expectedLength = 5;   // Standard commands
      }
    }
    
    if (rxIndex == expectedLength) {
      // Check if packet is for this slave
      if (rxBuffer[1] == SLAVE_ADDRESS) {
        const uint8_t cmd{rxBuffer[2]};
        
        if (cmd == CMD_SET_CONFIG) {
          // Handle config packet (17 bytes total)
          if (rxBuffer[16] == END_BYTE) {
            // Verify checksum
            uint8_t checksum = START_BYTE ^ SLAVE_ADDRESS ^ CMD_SET_CONFIG;
            for (int i = 3; i < 15; i++) {
              checksum ^= rxBuffer[i];
            }
            
            if (rxBuffer[15] == checksum) {
              handleConfigCommand(rxBuffer);
            }
          }
        } else {
          // Handle standard command (5 bytes total)
          if (rxBuffer[4] == END_BYTE) {
            const uint8_t checksum = START_BYTE ^ SLAVE_ADDRESS ^ cmd;
            
            if (rxBuffer[3] == checksum) {
              executeCommand(cmd);
            }
          }
        }
      }
      
      rxIndex = 0;
      expectedLength = 5;  // Reset to default
    }
  }
  
  delay(10);
}

// End of file /////////////////////////////////////////////////////////////////