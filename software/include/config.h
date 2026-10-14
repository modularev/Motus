#ifndef MOTUS_CONFIG_H
#define MOTUS_CONFIG_H

#include <Arduino.h>

// Sensor layout: channels 0-7 are sliders, channels 8-9 are XY pads
constexpr int NUM_SLIDERS = 8;
constexpr int NUM_PADS = 2;
constexpr int NUM_SENSORS = NUM_SLIDERS + NUM_PADS;

constexpr int ELECTRODES_PER_SENSOR = 12;
constexpr int ELECTRODES_PER_PAD_AXIS = 6;

// MPR121 ADDR lines driven by the MCP23017: the sensors above plus the button sensor
constexpr int NUM_SENSOR_SELECT_LINES = 11;
constexpr int BUTTON_SENSOR = 10; // touch buttons: set 1-4, fine, hold, load, save

enum Button
{
    BUTTON_SET_1,
    BUTTON_SET_2,
    BUTTON_SET_3,
    BUTTON_SET_4,
    BUTTON_FINE,
    BUTTON_HOLD,
    BUTTON_LOAD,
    BUTTON_SAVE,
    NUM_BUTTONS
};

// button sensor electrode of each button
constexpr uint8_t BUTTON_ELECTRODE[NUM_BUTTONS] = {0, 1, 2, 3, 4, 5, 6, 7};

// LEDs
constexpr int NUM_LED_DRIVERS = 4;
constexpr int LEDS_PER_DRIVER = 16;
constexpr int LEDS_PER_SLIDER = 6;
constexpr int LEDS_PER_PAD = 8;
constexpr int PAD_LED_DRIVER = 3;

// I2C
constexpr uint32_t I2C_CLOCK_HZ = 3400000;
constexpr uint8_t MCP23017_ADDR = 0x20;
constexpr uint8_t MPR121_ADDR_SELECTED = 0x5A; // ADDR pin low
constexpr uint8_t MPR121_ADDR_IDLE = 0x5B;     // ADDR pin tied to 3.3V
constexpr uint8_t LED_DRIVER_ADDR[NUM_LED_DRIVERS] = {0x40, 0x41, 0x42, 0x43};
constexpr int NUM_DAC_MUX_CHANNELS = 7;
constexpr int VAR_DAC_MUX_CHANNEL = 6; // var 1-4 on channels A-D

// Voltage presets: one per slider, each with the four var voltages
constexpr int NUM_PRESETS = NUM_SLIDERS;
constexpr int NUM_VARS = 4;

// Gate outputs: one per slider plus the master gate
constexpr uint8_t GATE_PINS[NUM_SLIDERS] = {9, 10, 11, 12, 13, 14, 15, 16};
constexpr uint8_t MASTER_GATE_PIN = 17;

// Encoder
constexpr uint8_t ENC_PIN_A = 23;
constexpr uint8_t ENC_PIN_B = 22;

// LCD
constexpr uint8_t LCD_RS_PIN = 5;
constexpr uint8_t LCD_RW_PIN = 7;
constexpr uint8_t LCD_ENABLE_PIN = 6;
constexpr uint8_t LCD_D4_PIN = 0;
constexpr uint8_t LCD_D5_PIN = 1;
constexpr uint8_t LCD_D6_PIN = 2;
constexpr uint8_t LCD_D7_PIN = 3;
constexpr int LCD_COLS = 8;
constexpr int LCD_ROWS = 2;

#endif
