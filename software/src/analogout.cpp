#include "analogout.h"

int gate_pin[] = {9, 10, 11, 12, 13, 14, 15, 16, 17};

Smoothed<unsigned int> interpol_pos[12];
Smoothed<unsigned int> interpol_pressure[10];

TCA9548A dacMux;
Adafruit_MCP4728 dac;

void dac_setup()
{
    dacMux.begin();
    dacMux.closeAll();

    for (int i = 0; i < 7; i++)
    {
        dacMux.openChannel(i);
        dac.begin();
        dacMux.closeChannel(i);
    }
    for (int i = 0; i < 12; i++)
    {
        interpol_pos[i].begin(SMOOTHED_EXPONENTIAL, 60);
    }

    for (int i = 0; i < 10; i++)
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

void setPressDAC(int channel, int value_8bit)
{
    interpol_pressure[channel].add(value_8bit << 3);
    value_8bit = interpol_pressure[channel].get();

    if (channel < 4)
    {
        dacMux.openChannel(2);
        dac.setChannelValue(static_cast<MCP4728_channel_t>(channel), value_8bit, MCP4728_VREF_INTERNAL);
        dacMux.closeChannel(2);
    }
    else if (channel < 8)
    {
        dacMux.openChannel(3);
        dac.setChannelValue(static_cast<MCP4728_channel_t>(channel % 4), value_8bit, MCP4728_VREF_INTERNAL);
        dacMux.closeChannel(3);
    }
    else if (channel == 8 || channel == 9)
    {
        dacMux.openChannel(5);
        dac.setChannelValue(static_cast<MCP4728_channel_t>(channel - 6), value_8bit, MCP4728_VREF_INTERNAL);
        dacMux.closeChannel(5);
    }
}

void setXYPadDAC(int channel, int x_value_9bit, int y_value_9bit)
{
    dacMux.openChannel(4);
    {

        if (channel == 8)
        {
            dac.setChannelValue(MCP4728_CHANNEL_A, x_value_9bit << 3, MCP4728_VREF_INTERNAL);
            dac.setChannelValue(MCP4728_CHANNEL_B, y_value_9bit << 3, MCP4728_VREF_INTERNAL);
        }

        else if (channel == 9)
        {
            dac.setChannelValue(MCP4728_CHANNEL_C, x_value_9bit << 3, MCP4728_VREF_INTERNAL);
            dac.setChannelValue(MCP4728_CHANNEL_D, y_value_9bit << 3, MCP4728_VREF_INTERNAL);
        }
    }
    dacMux.closeChannel(4);
}

void setMaster(int pos_10bit, int force_8bit)
{
    dacMux.openChannel(5);
    {
        dac.setChannelValue(MCP4728_CHANNEL_A, pos_10bit << 2, MCP4728_VREF_INTERNAL);
        dac.setChannelValue(MCP4728_CHANNEL_B, force_8bit << 4, MCP4728_VREF_INTERNAL);
    }
    dacMux.closeChannel(5);
}

void setGate(int channel, bool touchState)
{
    if (touchState && channel < 8)
    {
        digitalWrite(gate_pin[channel], LOW); // SET PIN LOW FOR NPN OFF / HIGH FOR DRIVING PNP NPN PULLING JACK LOW
        digitalWrite(gate_pin[8], LOW);
    }
    else if (touchState == 0)
    {
        digitalWrite(gate_pin[channel], HIGH);
        digitalWrite(gate_pin[8], HIGH);
    }
}