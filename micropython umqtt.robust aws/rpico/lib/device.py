from machine import Pin
import time

ENABLE_MOTION = True

PIN_DIRECTION = 20
PIN_STEP =      19
PIN_ENABLE_1 =  16
PIN_ENABLE_2 =  21
PIN_ENABLE_3 =  28

PULSES_PER_TURN =   1600
TURNS_PER_MOTION =  5.5
PULSES_PER_MOTION = PULSES_PER_TURN * TURNS_PER_MOTION

PULSE_HIGH_TIME =   20	    # us
PULSE_LOW_TIME =    200	    # us
PULSE_LOW_START =   5000    # us
PULSE_LOW_STEP =    100     # us

VALUE_OPEN =    0
VALUE_CLOSE =   1
VALUE_ENABLE =  0
VALUE_DISABLE = 1

blinds_are_open = False

pin_direction = Pin(PIN_DIRECTION, mode=Pin.OUT, value=VALUE_OPEN, pull=Pin.PULL_UP)
pin_step = Pin(PIN_STEP, mode=Pin.OUT, value=0, pull=Pin.PULL_DOWN)
pin_enable = [
    Pin(PIN_ENABLE_1, mode=Pin.OUT, value=VALUE_DISABLE, pull=Pin.PULL_UP),
    Pin(PIN_ENABLE_2, mode=Pin.OUT, value=VALUE_DISABLE, pull=Pin.PULL_UP),
    Pin(PIN_ENABLE_3, mode=Pin.OUT, value=VALUE_DISABLE, pull=Pin.PULL_UP)
]
    
def move():
    if ENABLE_MOTION:
        low_time = PULSE_LOW_START
        [pin.value(VALUE_ENABLE) for pin in pin_enable]
        for x in range(PULSES_PER_MOTION):
            pin_step.high()
            time.sleep_us(PULSE_HIGH_TIME)
            pin_step.low()
            time.sleep_us(low_time)
            low_time = (low_time - PULSE_LOW_STEP) if ((low_time - PULSE_LOW_STEP) >= PULSE_LOW_TIME) else (PULSE_LOW_TIME)
        [pin.value(VALUE_DISABLE) for pin in pin_enable]

def ForceCloseBlindsIntent():
    global blinds_are_open
    print("Closing the blinds ...")
    pin_direction.value(VALUE_CLOSE)
    move()
    blinds_are_open = False
    print("Blinds are closed")

def ForceOpenBlindsIntent():
    global blinds_are_open
    print("Opening the blinds ...")
    pin_direction.value(VALUE_OPEN)
    move()
    blinds_are_open = True
    print("Blinds are open")

def CloseBlindsIntent():
    global blinds_are_open
    if blinds_are_open:
        ForceCloseBlindsIntent()
    else:
        print("Blinds are already closed")

def OpenBlindsIntent():
    global blinds_are_open
    if not blinds_are_open:
        ForceOpenBlindsIntent()
    else:
        print("Blinds are already open")

def TestBlindsIntent():
    print("Test received")
    time.sleep(3)
