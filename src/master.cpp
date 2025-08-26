/*
 * ESP32-C3 RS485 Master - Simplified Window Blind Controller
 * 
 * Hardware:
 * - GPIO9: Button (control all blinds)
 * - GPIO8: LED (ON=System busy)
 * - GPIO0/2: RS485 TX/RX
 * - GPIO1: RS485 DE/RE
 */

#include <Arduino.h>
#include <HardwareSerial.h>
#include "config.h"
#include "button.h"

enum SystemState {
  IDLE,
  QUERYING,
  MOVING_SLAVE_1,
  MOVING_SLAVE_2,
  MOVING_SLAVE_3
};

// Function prototypes
void buttonAction();
void startOperation();
void querySlaves();
bool waitForResponse(uint8_t slave, uint8_t expectedCmd, uint8_t &data);
void sendCommand(uint8_t slave, uint8_t cmd);
void processIncoming();
void startNextSlave();

// Globals
HardwareSerial rs485(0);
SystemState systemState = IDLE;
bool slaveStates[NUM_SLAVES] = {false, false, false};  // false=closed, true=open
bool targetOpen = false;  // What we're moving blinds to
unsigned long currentSlaveStart = 0;  // Track start time for current slave
unsigned long lastHeartbeat = 0;

// Button instance
Button button(PIN_BTN, BUTTON_PRESS_LENGTH, Button::ACTIVE_LOW, buttonAction);

// Button callback
void buttonAction() {
  if (systemState == IDLE) {
    Serial.println("Master: Button pressed");
    startOperation();
  } else {
    Serial.println("Master: Button pressed but system busy - ignoring");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Master starting...");
  
  // Initialize button
  button.begin();
  
  pinMode(RS485_DE_RE, OUTPUT);
  digitalWrite(RS485_DE_RE, LOW);  // Receive mode
  
  rs485.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
  
  delay(1000);
  querySlaves();
  Serial.println("Ready. Press button to control all blinds.");
}

void loop() {
  // Update button (handles debouncing and triggers callback)
  button.update();
  
  // Heartbeat
  if (millis() - lastHeartbeat >= 5000) {
    lastHeartbeat = millis();
    Serial.printf("Master: State=%d Blinds=[", systemState);
    for (int i = 0; i < NUM_SLAVES; i++) {
      Serial.print(slaveStates[i] ? "OPEN" : "CLOSED");
      if (i < (NUM_SLAVES - 1)) Serial.print(",");
    }
    Serial.println("]");
  }
  
  // Process incoming messages
  processIncoming();
  
  // Timeout check - per slave, not entire operation
  if (systemState != IDLE) {
    if ((millis() - currentSlaveStart) > (MOTION_TIMEOUT + 1000U)) {
      Serial.printf("Master: Slave %d timeout - aborting entire operation\n", 
                    (systemState == MOVING_SLAVE_1) ? 1 : 
                    (systemState == MOVING_SLAVE_2) ? 2 : 3);
      systemState = IDLE;
    }
  }
  
  delay(10);
}

void startOperation() {
  querySlaves();
  
  // Check if all are in same state
  bool allOpen = true;
  bool allClosed = true;
  for (int i = 0; i < NUM_SLAVES; i++) {
    if (!slaveStates[i]) allOpen = false;
    if (slaveStates[i]) allClosed = false;
  }
  
  // Decide action
  if (allOpen) {
    Serial.println("Master: All open - will close all");
    targetOpen = false;
  } else if (allClosed) {
    Serial.println("Master: All closed - will open all");
    targetOpen = true;
  } else {
    Serial.println("Master: Mixed states - will close all for sync");
    targetOpen = false;
  }
  
  // Start with slave 1
  currentSlaveStart = millis();  // Track start time for timeout
  sendCommand(1, (targetOpen ? CMD_OPEN : CMD_CLOSE));
  systemState = MOVING_SLAVE_1;
  Serial.println("Master: Moving slave 1");
}

void querySlaves() {
  Serial.println("Master: Querying all slaves...");
  
  for (int i = 0; i < NUM_SLAVES; i++) {
    uint8_t slave = i + 1;
    sendCommand(slave, CMD_QUERY);
    
    uint8_t data;
    if (waitForResponse(slave, RSP_STATE, data)) {
      slaveStates[i] = (data == 1);
      Serial.printf("  Slave %d: %s\n", slave, (slaveStates[i] ? "OPEN" : "CLOSED"));
    } else {
      Serial.printf("  Slave %d: No response\n", slave);
    }
  }
}

void processIncoming() {
  static uint8_t rxBuffer[6];
  static uint8_t rxIndex = 0;
  
  while (rs485.available()) {
    uint8_t b = rs485.read();
    
    if ((rxIndex == 0) && (b != START_BYTE)) continue;
    
    rxBuffer[rxIndex++] = b;
    
    if (rxIndex == 6) {
      rxIndex = 0;
      
      if (rxBuffer[5] == END_BYTE) {
        uint8_t slave = rxBuffer[1];
        uint8_t cmd = rxBuffer[2];
        uint8_t data = rxBuffer[3];
        uint8_t checksum = rxBuffer[4];
        uint8_t expected = START_BYTE ^ slave ^ cmd ^ data;
        
        if (checksum == expected && cmd == RSP_COMPLETE) {
          Serial.printf("Master: Slave %d movement complete\n", slave);
          
          // Update state
          if (slave >= 1 && slave <= NUM_SLAVES) {
            slaveStates[slave-1] = (data == 1);
          }
          
          // Handle state machine
          if (((systemState == MOVING_SLAVE_1) && (slave == 1)) ||
              ((systemState == MOVING_SLAVE_2) && (slave == 2)) ||
              ((systemState == MOVING_SLAVE_3) && (slave == 3))) {
            startNextSlave();
          }
        }
      }
    }
  }
}

void startNextSlave() {
  switch (systemState) {
    case MOVING_SLAVE_1:
      currentSlaveStart = millis();  // Reset timer for slave 2
      sendCommand(2, (targetOpen ? CMD_OPEN : CMD_CLOSE));
      systemState = MOVING_SLAVE_2;
      Serial.println("Master: Moving slave 2");
      break;
      
    case MOVING_SLAVE_2:
      currentSlaveStart = millis();  // Reset timer for slave 3
      sendCommand(3, (targetOpen ? CMD_OPEN : CMD_CLOSE));
      systemState = MOVING_SLAVE_3;
      Serial.println("Master: Moving slave 3");
      break;
      
    case MOVING_SLAVE_3:
      Serial.println("Master: All operations complete");
      systemState = IDLE;
      break;
      
    default:
      break;
  }
}

bool waitForResponse(uint8_t slave, uint8_t expectedCmd, uint8_t &data) {
  unsigned long start = millis();
  uint8_t rxBuffer[6];
  uint8_t rxIndex = 0;
  
  while ((millis() - start) < RX_TIMEOUT) {
    if (rs485.available()) {
      uint8_t b = rs485.read();
      
      if (rxIndex == 0 && b != START_BYTE) continue;
      
      rxBuffer[rxIndex++] = b;
      
      if (rxIndex == 6) {
        if (rxBuffer[5] == END_BYTE &&
            rxBuffer[1] == slave &&
            rxBuffer[2] == expectedCmd) {
          uint8_t checksum = START_BYTE ^ slave ^ expectedCmd ^ rxBuffer[3];
          if (rxBuffer[4] == checksum) {
            data = rxBuffer[3];
            return true;
          }
        }
        rxIndex = 0;
      }
    }
    delay(1);
  }
  return false;
}

void sendCommand(uint8_t slave, uint8_t cmd) {
  digitalWrite(RS485_DE_RE, HIGH);
  delayMicroseconds(10);
  
  uint8_t checksum = START_BYTE ^ slave ^ cmd;
  
  rs485.write(START_BYTE);
  rs485.write(slave);
  rs485.write(cmd);
  rs485.write(checksum);
  rs485.write(END_BYTE);
  
  rs485.flush();
  delayMicroseconds(10);
  digitalWrite(RS485_DE_RE, LOW);
}

// End of file /////////////////////////////////////////////////////////////////
