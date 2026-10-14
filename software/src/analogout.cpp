#include "Motus/analogout.h"

#include <Arduino.h>
#include <Adafruit_MCP4728.h>
#include <Smoothed.h>
#include <TCA9548A.h>

#include "Motus/config.h"

static Smoothed<unsigned int> interpol_pressure[NUM_SENSORS];

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

    for (int i = 0; i < NUM_SENSORS; i++)
    {
        interpol_pressure[i].begin(SMOOTHED_EXPONENTIAL, 50);
    }
}

void setSliderDAC(int channel, int value_10bit)
{
    value_10bit = value_10bit << 2;

    if (channel < 4)
    {
        dacMux.openChannel(0);
        dac.setChannelValue(static_cast<MCP4728_channel_t>(channel), value_10bit, MCP4728_VREF_INTERNAL);
        dacMux.closeChannel(0);
    }
    else if (channel < 8)
    {
        dacMux.openChannel(1);
        dac.setChannelValue(static_cast<MCP4728_channel_t>(channel % 4), value_10bit, MCP4728_VREF_INTERNAL);
        dacMux.closeChannel(1);
    }
}

void setPressDAC(int channel, int value_9bit)
{
    interpol_pressure[channel].add(value_9bit << 3);
    value_9bit = interpol_pressure[channel].get();

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

void setMaster(int pos_10bit, int force_9bit)
{
    dacMux.openChannel(5);
    {
        dac.setChannelValue(MCP4728_CHANNEL_A, pos_10bit << 2, MCP4728_VREF_INTERNAL);
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
