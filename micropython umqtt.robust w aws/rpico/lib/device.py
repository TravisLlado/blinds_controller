from machine import Pin
from time import sleep_us

ENABLE_MOTION = True

PIN_DIRECTION = 16
PIN_STEP = 17
PIN_ENABLE_1 = 18
PIN_ENABLE_2 = 19
PIN_ENABLE_3 = 20

PULSES_PER_TURN = 1600
TURNS_PER_MOTION = 5.5
PULSES_PER_MOTION = PULSES_PER_TURN * TURNS_PER_MOTION

PULSE_HIGH_TIME = 20	# us
PULSE_LOW_TIME = 200	# us

VALUE_OPEN = 0
VALUE_CLOSE = 1
VALUE_ENABLE = 0
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
        for i in range(0,3):
            pin_enable[i].value(VALUE_ENABLE)
            for x in range(PULSES_PER_MOTION):
                pin_step.high()
                sleep_us(PULSE_HIGH_TIME)
                pin_step.low()
                sleep_us(PULSE_LOW_TIME)
            pin_enable[i].value(VALUE_DISABLE)

def force_open():
    global blinds_are_open
    print("Opening the blinds ...")
    pin_direction.value(VALUE_OPEN)
    move()
    blinds_are_open = True
    print("Blinds are open")

def force_close():
    global blinds_are_open
    print("Closing the blinds ...")
    pin_direction.value(VALUE_CLOSE)
    move()
    blinds_are_open = False
    print("Blinds are closed")

def open():
    global blinds_are_open
    if not blinds_are_open:
        force_open()
    else:
        print("Blinds are already open")

def close():
    global blinds_are_open
    if blinds_are_open:
        force_close()
    else:
        print("Blinds are already closed")

commands = {
    "\"open\"":        open,
    "\"close\"":       close,
    "\"force_open\"":  force_open,
    "\"force_close\"": force_close
}
