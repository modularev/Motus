#include "sensor-processing.h"

int16_t filteredValues[12][12] = {0};
int16_t baselineValues[12][12] = {0};
int16_t touchStrength[12][12] = {0};

int sliderPos[12] = {0};

int8_t max_index = 0;
uint16_t max_value = 0;
uint16_t centroid = 0;
uint16_t x_centroid = 0;
uint16_t y_centroid = 0;
uint16_t pressure = 0;
uint16_t xy_pressure = 0;
int touchState = 0;
bool touchcheck = false;

Adafruit_MPR121 cap = Adafruit_MPR121();
MCP23017 mcp = MCP23017(0x20);

void sensorSetup()
{
    // initialize MCP23018 GPIO Expander as 16 outputs
    mcp.init();
    mcp.portMode(MCP23017Port::A, 0);
    mcp.portMode(MCP23017Port::B, 0);

    // set ADDR pins on MPR121 HIGH
    for (int i = 0; i < 11; i++)
    {
        mcp.digitalWrite(i, HIGH);
    }

    // Default address is 0x5A, if tied to 3.3V its 0x5B
    if (!cap.begin(0x5B))
    {
        Serial.println("MPR121 not found, check wiring?");
        while (1)
            ;
    }
    for (int i = 0; i < 11; i++)
    {
        mcp.digitalWrite(i, LOW);
        cap.begin(0x5A);
        cap.setThresholds(60, 20);
        cap.writeRegister(0x5E, 01);
        mcp.digitalWrite(i, HIGH);
    }
}
void sensorCheck()
{
    for (int sensorChan = 0; sensorChan < 12; sensorChan++)
    {
        // int chanMask = 1 << (sensorChan - 1);
        // if (tchChannels & chanMask)
        sensorRead(sensorChan);
    }
}

float getInterpolatedPosition(int16_t touchStrength[], uint8_t length)
{
    // Find the maximum touch strength
    int16_t maxTouch = 0;
    uint8_t maxIndex = 0;

    for (uint8_t i = 0; i < length; i++)
    {
        if (touchStrength[i] > maxTouch)
        {
            maxTouch = touchStrength[i];
            maxIndex = i;
        }
    }

    // If no touch detected, return -1
    if (maxTouch == 0)
        return -1;

    // Linear interpolation
    float position = 0;
    if (maxIndex > 0 && maxIndex < length - 1)
    {
        // Interpolate between the neighboring electrodes
        int16_t leftValue = touchStrength[maxIndex - 1];
        int16_t rightValue = touchStrength[maxIndex + 1];

        // Introduce a negative factor to slow down the rate of change
        float factor = 1.4; // Adjust as needed

        // Calculate the interpolated position with the negative factor
        int16_t sum = leftValue + touchStrength[maxIndex] + rightValue;
        if (sum > 0)
        {
            // Calculate the raw interpolated position
            float rawPosition = maxIndex + factor * (float)(rightValue - leftValue) / sum;

            // Clamp the raw interpolated position to stay within the valid range
            position = max(0.0f, min(length - 1.0f, rawPosition));
        }
        else
        {
            position = maxIndex;
        }
    }
    else
    {
        position = maxIndex;
    }

    // Scale position to 10-bit value
    float scaledPosition = position / (length - 1) * 1023.0;

    return (u_int16_t)scaledPosition & 1023;
}

float onePoleFilter(int input, int &prevOutput, float alpha)
{
    // Apply one-pole filter equation
    float output = alpha * input + (1 - alpha) * prevOutput;
    prevOutput = output; // Update previous output for next iteration
    return output;
}

void sensorRead(int sensorChan)
{
    // select corresponding sensor
    mcp.digitalWrite(sensorChan, LOW);
    touchState = cap.touched();
    setGate(sensorChan, touchState);

    // Read filtered and baseline values
    if (touchState)
    {
        for (int i = 0; i < 12; i++)
        {
            filteredValues[sensorChan][i] = cap.filteredData(i);
            baselineValues[sensorChan][i] = cap.baselineData(i);
            touchStrength[sensorChan][i] = baselineValues[sensorChan][i] - filteredValues[sensorChan][i];
        }
        int sensorAmt = (sensorChan < 8) ? 12 : 6;
        int position = (int)getInterpolatedPosition(touchStrength[sensorChan], sensorAmt);
        int pressure = getPress(touchStrength[sensorChan]);

        switch (sensorAmt)
        {
        case 12: // Slider with 12 electrodes
            position = onePoleFilter(position, sliderPos[sensorChan], 0.4333);
            Serial.println((String) "sensorChan: " + sensorChan + "\tposition: " + position + "\t");
            // interpol_pos[sensorChan].add(position);

            setSliderDAC(sensorChan, position);
            setPressDAC(sensorChan, pressure);
            setSliderLED(sensorChan, position);
            setMaster(position, pressure);
            display_value(sensorChan, position, pressure);
            break;

        case 6: // XYPad with 6 electrodes per Axis
            int16_t touchStrengthX[6];
            int16_t touchStrengthY[6];
            for (int i = 0; i < 6; i++)
            {
                touchStrengthX[i] = touchStrength[sensorChan][i];
                touchStrengthY[i] = touchStrength[sensorChan][i + 6];
            }
            int positionX = (int)getInterpolatedPosition(touchStrengthX, sensorAmt);
            int positionY = (int)getInterpolatedPosition(touchStrengthY, sensorAmt);

            setXYPadLED(sensorChan, positionX, positionY);

            // INVERT BECAUSE OF CHANNEL LAYOUT
            // if (sensorChan == 9)
            // {
            //     positionX = 1023 - positionX;
            //     positionY = 1023 - positionY;
            // }
            setXYPadDAC(sensorChan, positionX, positionY);
            // display_value_xy(sensorChan, positionX, positionY, pressure);

            break;
        }
        // usbMIDI.sendControlChange(sensorChan, centroid / 8, 1);
    }
    else
    {
        setPressDAC(sensorChan, 0);
    }
    // deselect sensor
    mcp.digitalWrite(sensorChan, HIGH);
}

int getPress(int16_t touchStrength[])
{
    const int pressure_threshhold = 1200;

    for (int i = 0; i < 12; i++)
    {
        pressure += touchStrength[i];
    }

    if (pressure < pressure_threshhold)
    {
        pressure = 0;
    }
    else
    {
        pressure -= pressure_threshhold;
    }
    if (pressure > 511)
    {
        pressure = 511;
    }
    pressure = (pressure * pressure) >> 9;
    return pressure;
}

uint16_t check_upper(int index)
{
    if (index > 11)
        return 0;
    else
        return stored_data[index];
}

uint16_t check_upper_x(int index)
{
    if (index > 5)
        return 0;
    else
        return stored_data[index];
}

uint16_t check_lower(int index)
{
    if (index < 0)
        return 0;
    else
        return stored_data[index];
}

uint16_t check_lower_y(int index)
{
    if (index < 6)
        return 0;
    else
        return stored_data[index];
}
