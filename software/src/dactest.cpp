#ifdef MOTUS_DAC_TEST

#include <Arduino.h>

#include "analogout.h"
#include "config.h"
#include "display.h"
#include "sensors.h"

// DAC test: every DAC output is set to the same value, chosen with the
// SET pads, so the output voltages can be measured with a voltmeter.
void dacTest()
{
    static const int VALUES[4] = {0, 1365, 2730, 4095};
    int value = 0;
    setAllDACs(value);

    while (true)
    {
        uint8_t buttons = buttonsRead();
        for (int i = 0; i < 4; i++)
        {
            if ((buttons & (1 << (BUTTON_SET_1 + i))) && VALUES[i] != value)
            {
                value = VALUES[i];
                setAllDACs(value);
            }
        }

        char line[9];
        snprintf(line, sizeof(line), "    %4d", value);
        displayMessage("DAC TEST", line);
        displayUpdate();
    }
}

#endif
