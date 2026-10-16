#include "analogout.h"

#include <Arduino.h>
#include <Adafruit_MCP4728.h>
#include <TCA9548A.h>

#include "config.h"

// pressure outputs are smoothed: each new value is averaged with the previous one
static uint16_t smoothedPressure[NUM_SENSORS];

// Output calibration: measured voltage at DAC values 0, 1365, 2730 and 4095
// (with the DAC test build), per output. Voltages are converted to DAC values
// by interpolating between these points.
static const int CAL_POINTS = 4;
static const int CAL_CODES[CAL_POINTS] = {0, 1365, 2730, 4095};
static const float SLIDER_CAL_VOLTS[NUM_SLIDERS][CAL_POINTS] = {
    {0.008, 2.729, 5.452, 8.17},
    {0.008, 2.748, 5.476, 8.20},
    {0.007, 2.738, 5.460, 8.19},
    {0.006, 2.747, 5.472, 8.19},
    {0.015, 2.750, 5.476, 8.21},
    {0.008, 2.721, 5.457, 8.19},
    {0.007, 2.726, 5.448, 8.18},
    {0.008, 2.750, 5.494, 8.21},
};
static const float VAR_CAL_VOLTS[NUM_VARS][CAL_POINTS] = {
    {0.007, 2.740, 5.478, 8.20},
    {0.011, 2.753, 5.481, 8.21},
    {0.006, 2.744, 5.466, 8.18},
    {0.007, 2.727, 5.456, 8.18},
};

static TCA9548A dacMux;
static Adafruit_MCP4728 dac;

void dacSetup()
{
    for (int i = 0; i < NUM_SLIDERS; i++)
    {
        pinMode(GATE_PINS[i], OUTPUT);
        digitalWrite(GATE_PINS[i], HIGH);
    }
    pinMode(MASTER_GATE_PIN, OUTPUT);
    digitalWrite(MASTER_GATE_PIN, HIGH);

    dacMux.begin();
    dacMux.closeAll();

    for (int i = 0; i < NUM_DAC_MUX_CHANNELS; i++)
    {
        dacMux.openChannel(i);
        dac.begin();
        dacMux.closeChannel(i);
    }
}

// position output of a slider, full 12 bit
static void setSliderDAC12(int channel, int value_12bit)
{
    dacMux.openChannel(channel < 4 ? 0 : 1);
    dac.setChannelValue(static_cast<MCP4728_channel_t>(channel % 4), value_12bit, MCP4728_VREF_INTERNAL);
    dacMux.closeChannel(channel < 4 ? 0 : 1);
}

void setSliderDAC(int channel, int value_10bit)
{
    if (channel < NUM_SLIDERS)
        setSliderDAC12(channel, value_10bit << 2);
}

void setPressDAC(int channel, int value_9bit)
{
    smoothedPressure[channel] = (smoothedPressure[channel] + (value_9bit << 3)) / 2;
    value_9bit = smoothedPressure[channel];

    if (channel < 4)
    {
        dacMux.openChannel(2);
        dac.setChannelValue(static_cast<MCP4728_channel_t>(channel), value_9bit, MCP4728_VREF_INTERNAL);
        dacMux.closeChannel(2);
    }
    else if (channel < 8)
    {
        dacMux.openChannel(3);
        dac.setChannelValue(static_cast<MCP4728_channel_t>(channel % 4), value_9bit, MCP4728_VREF_INTERNAL);
        dacMux.closeChannel(3);
    }
    else if (channel == 8 || channel == 9)
    {
        dacMux.openChannel(5);
        dac.setChannelValue(static_cast<MCP4728_channel_t>(channel - 6), value_9bit, MCP4728_VREF_INTERNAL);
        dacMux.closeChannel(5);
    }
}

void setXYPadDAC(int channel, int x_value_10bit, int y_value_10bit)
{
    // DAC outputs are wired A = X1, B = X2, C = Y1, D = Y2
    dacMux.openChannel(4);
    {

        if (channel == 8)
        {
            dac.setChannelValue(MCP4728_CHANNEL_A, x_value_10bit << 2, MCP4728_VREF_INTERNAL);
            dac.setChannelValue(MCP4728_CHANNEL_C, y_value_10bit << 2, MCP4728_VREF_INTERNAL);
        }

        else if (channel == 9)
        {
            dac.setChannelValue(MCP4728_CHANNEL_B, x_value_10bit << 2, MCP4728_VREF_INTERNAL);
            dac.setChannelValue(MCP4728_CHANNEL_D, y_value_10bit << 2, MCP4728_VREF_INTERNAL);
        }
    }
    dacMux.closeChannel(4);
}

void setMaster(int pos_12bit, int force_9bit)
{
    dacMux.openChannel(5);
    {
        dac.setChannelValue(MCP4728_CHANNEL_A, constrain(pos_12bit, 0, 4095), MCP4728_VREF_INTERNAL);
        dac.setChannelValue(MCP4728_CHANNEL_B, force_9bit << 3, MCP4728_VREF_INTERNAL);
    }
    dacMux.closeChannel(5);
}

void setGate(int channel, bool touchState)
{
    // SET PIN LOW FOR NPN OFF / HIGH FOR DRIVING PNP NPN PULLING JACK LOW
    if (channel < NUM_SLIDERS)
        digitalWrite(GATE_PINS[channel], touchState ? LOW : HIGH);
}

void setMasterGate(bool touchState)
{
    digitalWrite(MASTER_GATE_PIN, touchState ? LOW : HIGH);
}

void setVarDAC(int var, int value_12bit)
{
    dacMux.openChannel(VAR_DAC_MUX_CHANNEL);
    dac.setChannelValue(static_cast<MCP4728_channel_t>(var), constrain(value_12bit, 0, 4095), MCP4728_VREF_INTERNAL);
    dacMux.closeChannel(VAR_DAC_MUX_CHANNEL);
}

void setAllDACs(int value_12bit)
{
    for (int mux = 0; mux < NUM_DAC_MUX_CHANNELS; mux++)
    {
        dacMux.openChannel(mux);
        for (int channel = 0; channel < 4; channel++)
            dac.setChannelValue(static_cast<MCP4728_channel_t>(channel), constrain(value_12bit, 0, 4095), MCP4728_VREF_INTERNAL);
        dacMux.closeChannel(mux);
    }
}

static int voltsToCode(const float cal[CAL_POINTS], float volts)
{
    int i = 0;
    while (i < CAL_POINTS - 2 && volts > cal[i + 1])
        i++;
    float t = (volts - cal[i]) / (cal[i + 1] - cal[i]);
    float code = CAL_CODES[i] + t * (CAL_CODES[i + 1] - CAL_CODES[i]);
    return constrain(lroundf(code), 0, 4095);
}

int setSliderVolts(int channel, float volts)
{
    int code = voltsToCode(SLIDER_CAL_VOLTS[channel], volts);
    setSliderDAC12(channel, code);
    return code;
}

void setVarVolts(int var, float volts)
{
    setVarDAC(var, voltsToCode(VAR_CAL_VOLTS[var], volts));
}
