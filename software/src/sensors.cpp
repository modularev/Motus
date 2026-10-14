#include "Motus/sensors.h"

#include <Arduino.h>
#include <Adafruit_MPR121.h>
#include <MCP23017.h>
#include <Wire.h>

static Adafruit_MPR121 cap = Adafruit_MPR121();
static MCP23017 mcp = MCP23017(MCP23017_ADDR);

// MPR121 registers
static const uint8_t MPR121_REG_FILTERED = 0x04;
static const uint8_t MPR121_REG_ECR = 0x5E;
static const uint8_t MPR121_ECR_STOP = 0x00;
static const uint8_t MPR121_ECR_RUN = 0x4C; // 12 electrodes, baseline tracking off

// Touch detection, on the summed electrode strength above the noise floor
static const int TOUCH_ON = 120;
static const int TOUCH_OFF = 60;

// Button sensor, per electrode
static const int BUTTON_ON = 60;
static const int BUTTON_OFF = 30;

// Baseline tracking, per scan while the sensor is not touched
static const float BASELINE_RATE = 0.001f;        // slow drift compensation
static const float BASELINE_RATE_RISING = 0.05f;  // fast recovery if the baseline is too low
static const int BASELINE_SAMPLES = 16;

// Position tuning
static const int NOISE_FLOOR = 16;         // subtracted from every electrode before the centroid
static const float EDGE_MARGIN = 0.45f;    // electrodes cut off at each end so the ends reach 0 and 1023

// Position smoothing: an adaptive low-pass (1€ filter) that smooths heavily
// when the finger is slow and hardly at all when it moves fast, followed by
// a little play so a resting finger's slow wobble does not move the output.
static const float MIN_CUTOFF_HZ = 3.0f;   // smoothing of a resting finger
static const float SPEED_COEFF = 0.05f;    // extra cutoff (Hz) per count/s of finger speed
static const float SPEED_CUTOFF_HZ = 1.0f; // smoothing of the speed estimate
static const float PLAY = 6.0f;            // counts the finger can wobble without moving the output

// Lifting a finger shifts the position as the contact shrinks. On release the
// position is set back to its value from before the contact started shrinking.
static const uint32_t LIFT_WINDOW_MS = 100; // how far back the lift is searched for
static const float LIFT_RATIO = 0.9f;       // contact below this share of its recent maximum counts as lifting
static const float LIFT_MAX_SPEED = 500.0f; // counts/s; a finger lifted while sliding faster keeps its last position

// XY pad Y axis: sensor electrode at each physical position, bottom to top
// (pad-local, the second pad is mounted rotated by 180 degrees)
static const uint8_t PAD_Y_ELECTRODE[ELECTRODES_PER_PAD_AXIS] = {6, 7, 11, 10, 9, 8};

struct AxisFilter
{
    float x;   // smoothed position
    float dx;  // smoothed speed, counts/s
    float out; // output after the play
};

struct HistoryEntry
{
    uint32_t ms;
    int strength;
    float speed;
    float pos[2];
};

static const int HISTORY_LENGTH = 64;

static AxisFilter axisFilter[NUM_SENSORS][2];
static uint32_t lastSampleUs[NUM_SENSORS];
static HistoryEntry history[NUM_SENSORS][HISTORY_LENGTH];
static uint8_t historyHead[NUM_SENSORS];
static uint8_t historyCount[NUM_SENSORS];
static bool wasTouched[NUM_SENSORS] = {false};

// The MPR121's own baseline follows a finger that approaches slowly, which
// swallows the partial signal on neighbouring electrodes. So its tracking is
// switched off and the baseline is kept here instead.
static float baseline[NUM_SENSOR_SELECT_LINES][ELECTRODES_PER_SENSOR];

#ifdef MOTUS_DEBUG
static uint8_t ecrReadback[NUM_SENSOR_SELECT_LINES] = {0};
static elapsedMillis sinceConfigPrint;
#endif

// read the filtered data of all 12 electrodes of the selected sensor in one transfer
static bool readFiltered(uint16_t filtered[])
{
    const uint8_t count = ELECTRODES_PER_SENSOR * 2;

    Wire.beginTransmission(MPR121_ADDR_SELECTED);
    Wire.write(MPR121_REG_FILTERED);
    if (Wire.endTransmission(false) != 0)
        return false;
    if (Wire.requestFrom(MPR121_ADDR_SELECTED, count) != count)
        return false;

    for (int i = 0; i < ELECTRODES_PER_SENSOR; i++)
    {
        uint8_t low = Wire.read();
        uint8_t high = Wire.read();
        filtered[i] = (low | (high << 8)) & 0x3FF;
    }
    return true;
}

void sensorSetup()
{
    // initialize MCP23018 GPIO Expander as 16 outputs
    mcp.init();
    mcp.portMode(MCP23017Port::A, 0);
    mcp.portMode(MCP23017Port::B, 0);

    // set ADDR pins on MPR121 HIGH
    for (int i = 0; i < NUM_SENSOR_SELECT_LINES; i++)
    {
        mcp.digitalWrite(i, HIGH);
    }

    // Default address is 0x5A, if tied to 3.3V its 0x5B
    if (!cap.begin(MPR121_ADDR_IDLE))
    {
        Serial.println("MPR121 not found, check wiring?");
        while (1)
            ;
    }
    for (int i = 0; i < NUM_SENSOR_SELECT_LINES; i++)
    {
        mcp.digitalWrite(i, LOW);
        cap.begin(MPR121_ADDR_SELECTED);
        cap.writeRegister(MPR121_REG_ECR, MPR121_ECR_STOP);
        cap.writeRegister(MPR121_REG_ECR, MPR121_ECR_RUN);
#ifdef MOTUS_DEBUG
        ecrReadback[i] = cap.readRegister8(MPR121_REG_ECR);
#endif
        mcp.digitalWrite(i, HIGH);
    }

    // take the untouched level of every electrode as its baseline
    delay(100);
    for (int channel = 0; channel < NUM_SENSOR_SELECT_LINES; channel++)
    {
        long sum[ELECTRODES_PER_SENSOR] = {0};
        int samples = 0;

        mcp.digitalWrite(channel, LOW);
        for (int n = 0; n < BASELINE_SAMPLES; n++)
        {
            uint16_t filtered[ELECTRODES_PER_SENSOR];
            if (readFiltered(filtered))
            {
                for (int i = 0; i < ELECTRODES_PER_SENSOR; i++)
                    sum[i] += filtered[i];
                samples++;
            }
            delay(5);
        }
        mcp.digitalWrite(channel, HIGH);

        for (int i = 0; i < ELECTRODES_PER_SENSOR; i++)
            baseline[channel][i] = samples ? (float)sum[i] / samples : 0;
    }
}

// Weighted centroid of all electrodes above the noise floor, scaled to 10 bit.
// Electrodes enter and leave the sum at zero weight, so the result is
// continuous across electrode boundaries. Returns false if nothing is above
// the floor, leaving position untouched.
static bool getCentroid(const int16_t touchStrength[], uint8_t length, float &position)
{
    long weighted = 0;
    long sum = 0;

    for (uint8_t i = 0; i < length; i++)
    {
        int strength = touchStrength[i] - NOISE_FLOOR;
        if (strength > 0)
        {
            weighted += (long)i * strength;
            sum += strength;
        }
    }

    if (sum == 0)
        return false;

    // centroid in electrode units, 0 .. length - 1
    float centroid = (float)weighted / sum;

    // stretch so the ends are reachable, then scale to 10 bit
    float span = (length - 1) - 2 * EDGE_MARGIN;
    float scaled = (centroid - EDGE_MARGIN) / span * 1023.0f;
    position = max(0.0f, min(1023.0f, scaled));
    return true;
}

// Pressure is read from the contact area: a firmer press flattens the finger
// over more electrodes. The contact is measured as the root of the summed
// squared electrode strengths, which hardly changes while sliding across
// electrodes but rises clearly when pressing.
//
// Pressing also shifts the measured position, because the finger flattens
// downwards, towards the player. The shift grows with the pressure, so it is
// taken back out using the shift measured at a firm press.
//
// Calibrated at a light touch and a firm press at the bottom, middle and top,
// shared by all sliders. At the bottom end the finger spreads off the slider,
// so there is little pressure range there.
struct PressurePoint
{
    float position; // slider position at a light touch, 10 bit
    float light;    // contact at a light touch -> pressure 0
    float firm;     // contact at a firm press -> pressure 511
    float shift;    // position shift from a light touch to a firm press
};

static const PressurePoint PRESSURE_CAL[] = {
    {2, 332, 396, 0},
    {484, 383, 618, -60},
    {1017, 356, 591, -106},
};
static const int PRESSURE_CAL_POINTS = sizeof(PRESSURE_CAL) / sizeof(PRESSURE_CAL[0]);
static const float PRESSURE_COMP_MAX = 1.3f; // pressing harder than firm is compensated up to this much

// XY pads: contact at a light touch and a firm press, the same across the pad
static const float PAD_PRESSURE_LIGHT = 316;
static const float PAD_PRESSURE_FIRM = 476;

static float getContact(const int16_t touchStrength[])
{
    float sum = 0;
    for (int i = 0; i < ELECTRODES_PER_SENSOR; i++)
    {
        float strength = touchStrength[i] - NOISE_FLOOR;
        if (strength > 0)
            sum += strength * strength;
    }
    return sqrtf(sum);
}

// pressure as a share of a firm press (0 .. PRESSURE_COMP_MAX), and the
// position shift that pressure causes
static float getSliderPressure(float contact, float position, float &shift)
{
    // find the calibration points on either side of the position
    int i = 0;
    while (i < PRESSURE_CAL_POINTS - 2 && position > PRESSURE_CAL[i + 1].position)
        i++;
    const PressurePoint &a = PRESSURE_CAL[i];
    const PressurePoint &b = PRESSURE_CAL[i + 1];

    float t = constrain((position - a.position) / (b.position - a.position), 0.0f, 1.0f);
    float light = a.light + t * (b.light - a.light);
    float firm = a.firm + t * (b.firm - a.firm);

    float pressure = constrain((contact - light) / (firm - light), 0.0f, PRESSURE_COMP_MAX);
    shift = pressure * (a.shift + t * (b.shift - a.shift));
    return pressure;
}

// position with the shift from pressing taken out, and the pressure (0 .. 511)
static float compensatePressure(const int16_t touchStrength[], float position, int &pressure)
{
    float contact = getContact(touchStrength);
    float shift;

    // the shift depends on where the finger really is, so estimate that first
    getSliderPressure(contact, position, shift);
    float share = getSliderPressure(contact, position - shift, shift);

    pressure = min(share, 1.0f) * 511;
    return constrain(position - shift, 0.0f, 1023.0f);
}

static float smoothingFactor(float dt, float cutoffHz)
{
    float r = 2 * PI * cutoffHz * dt;
    return r / (r + 1);
}

static void startAxis(AxisFilter &f, float raw)
{
    f.x = raw;
    f.dx = 0;
    f.out = raw;
}

static float updateAxis(AxisFilter &f, float raw, float dt)
{
    float speed = (raw - f.x) / dt;
    f.dx += smoothingFactor(dt, SPEED_CUTOFF_HZ) * (speed - f.dx);
    float cutoff = MIN_CUTOFF_HZ + SPEED_COEFF * fabsf(f.dx);
    f.x += smoothingFactor(dt, cutoff) * (raw - f.x);

    float error = f.x - f.out;
    if (error > PLAY)
        f.out = f.x - PLAY;
    else if (error < -PLAY)
        f.out = f.x + PLAY;
    return f.out;
}

static void pushHistory(int channel, int strength, float speed, const float pos[2])
{
    HistoryEntry &e = history[channel][historyHead[channel]];
    e.ms = millis();
    e.strength = strength;
    e.speed = speed;
    e.pos[0] = pos[0];
    e.pos[1] = pos[1];
    historyHead[channel] = (historyHead[channel] + 1) % HISTORY_LENGTH;
    if (historyCount[channel] < HISTORY_LENGTH)
        historyCount[channel]++;
}

// position from just before the contact started shrinking
static void positionBeforeLift(int channel, float pos[2])
{
    uint32_t now = millis();
    int maxStrength = 0;
    for (int k = 0; k < historyCount[channel]; k++)
    {
        const HistoryEntry &e = history[channel][(historyHead[channel] + HISTORY_LENGTH - 1 - k) % HISTORY_LENGTH];
        if (now - e.ms > LIFT_WINDOW_MS)
            break;
        maxStrength = max(maxStrength, e.strength);
    }

    // the most recent sample that still had most of the contact
    for (int k = 0; k < historyCount[channel]; k++)
    {
        const HistoryEntry &e = history[channel][(historyHead[channel] + HISTORY_LENGTH - 1 - k) % HISTORY_LENGTH];
        if (now - e.ms > LIFT_WINDOW_MS)
            break;
        if (e.strength >= LIFT_RATIO * maxStrength)
        {
            // the finger was sliding, not resting: the last position is the intended one
            if (e.speed > LIFT_MAX_SPEED)
                return;
            pos[0] = e.pos[0];
            pos[1] = e.pos[1];
            return;
        }
    }
}

#ifdef MOTUS_DEBUG
// D,<ms>,<channel>,<12 electrode deltas>,<raw pos | x>,<filtered pos | y>,<pressure>
static void debugPrint(int channel, const int16_t touchStrength[], float a, float b, int pressure)
{
    Serial.printf("D,%lu,%d", millis(), channel);
    for (int i = 0; i < ELECTRODES_PER_SENSOR; i++)
    {
        Serial.printf(",%d", touchStrength[i]);
    }
    Serial.printf(",%.1f,%.1f,%d\n", a, b, pressure);
}

// C,<ms>,<ECR register of each MPR121 as read back in setup>
static void debugPrintConfig()
{
    if (sinceConfigPrint < 5000)
        return;
    sinceConfigPrint = 0;

    Serial.printf("C,%lu", millis());
    for (int i = 0; i < NUM_SENSOR_SELECT_LINES; i++)
    {
        Serial.printf(",0x%02X", ecrReadback[i]);
    }
    Serial.println();
}
#endif

TouchReading sensorRead(int channel)
{
    TouchReading reading = {false, false, false, 0, 0, 0, 0};

    // select corresponding sensor, read it, deselect
    uint16_t filtered[ELECTRODES_PER_SENSOR];
    mcp.digitalWrite(channel, LOW);
    bool ok = readFiltered(filtered);
    mcp.digitalWrite(channel, HIGH);

    int16_t touchStrength[ELECTRODES_PER_SENSOR];
    int strength = 0;
    if (ok)
    {
        for (int i = 0; i < ELECTRODES_PER_SENSOR; i++)
        {
            touchStrength[i] = lroundf(baseline[channel][i] - filtered[i]);
            if (touchStrength[i] > NOISE_FLOOR)
                strength += touchStrength[i] - NOISE_FLOOR;
        }
    }

    // hysteresis so a touch does not flicker around the threshold
    reading.touched = strength >= (wasTouched[channel] ? TOUCH_OFF : TOUCH_ON);
    reading.started = reading.touched && !wasTouched[channel];

    if (ok && !reading.touched)
    {
        for (int i = 0; i < ELECTRODES_PER_SENSOR; i++)
        {
            float error = filtered[i] - baseline[channel][i];
            baseline[channel][i] += error * (error > NOISE_FLOOR ? BASELINE_RATE_RISING : BASELINE_RATE);
        }
    }

    if (reading.touched)
    {
        // raw position per axis: sliders use one, XY pads two
        int axes = isSlider(channel) ? 1 : 2;
        float raw[2] = {axisFilter[channel][0].x, axisFilter[channel][1].x};

        if (isSlider(channel))
        {
            if (getCentroid(touchStrength, ELECTRODES_PER_SENSOR, raw[0]))
                raw[0] = compensatePressure(touchStrength, raw[0], reading.pressure);
        }
        else
        {
            // XY pad: first 6 electrodes are the X axis, last 6 the Y axis
            int16_t touchStrengthY[ELECTRODES_PER_PAD_AXIS];
            for (int i = 0; i < ELECTRODES_PER_PAD_AXIS; i++)
            {
                touchStrengthY[i] = touchStrength[PAD_Y_ELECTRODE[i]];
            }
            getCentroid(touchStrength, ELECTRODES_PER_PAD_AXIS, raw[0]);
            getCentroid(touchStrengthY, ELECTRODES_PER_PAD_AXIS, raw[1]);

            float share = (getContact(touchStrength) - PAD_PRESSURE_LIGHT) / (PAD_PRESSURE_FIRM - PAD_PRESSURE_LIGHT);
            reading.pressure = constrain(share, 0.0f, 1.0f) * 511;
        }

        uint32_t nowUs = micros();
        float dt = constrain((nowUs - lastSampleUs[channel]) * 1e-6f, 0.0005f, 0.1f);
        lastSampleUs[channel] = nowUs;

        float out[2] = {0, 0};
        float speed = 0;
        for (int axis = 0; axis < axes; axis++)
        {
            // start a new touch at its own position instead of gliding from the last one
            if (!wasTouched[channel])
                startAxis(axisFilter[channel][axis], raw[axis]);
            out[axis] = updateAxis(axisFilter[channel][axis], raw[axis], dt);
            speed = max(speed, fabsf(axisFilter[channel][axis].dx));
        }

        if (!wasTouched[channel])
            historyCount[channel] = 0;
        pushHistory(channel, strength, speed, out);

        reading.position = reading.x = lroundf(out[0]);
        reading.y = lroundf(out[1]);
#ifdef MOTUS_DEBUG
        if (axes == 1)
            debugPrint(channel, touchStrength, raw[0], out[0], reading.pressure);
        else
            debugPrint(channel, touchStrength, out[0], out[1], reading.pressure);
#endif
    }
    else if (wasTouched[channel])
    {
        // released: report the position from before the finger started lifting
        float pos[2] = {axisFilter[channel][0].out, axisFilter[channel][1].out};
        positionBeforeLift(channel, pos);
        reading.released = true;
        reading.position = reading.x = lroundf(pos[0]);
        reading.y = lroundf(pos[1]);
#ifdef MOTUS_DEBUG
        Serial.printf("R,%lu,%d,%.1f,%.1f\n", millis(), channel, pos[0], pos[1]);
#endif
    }
    wasTouched[channel] = reading.touched;

#ifdef MOTUS_DEBUG
    debugPrintConfig();
#endif

    return reading;
}

// Button sensor: every electrode is its own button, touched or not.
// Returns a bit per electrode.
uint16_t buttonsRead()
{
    static uint16_t touched = 0;

    uint16_t filtered[ELECTRODES_PER_SENSOR];
    mcp.digitalWrite(BUTTON_SENSOR, LOW);
    bool ok = readFiltered(filtered);
    mcp.digitalWrite(BUTTON_SENSOR, HIGH);
    if (!ok)
        return touched;

#ifdef MOTUS_DEBUG
    bool anyActive = false;
#endif
    for (int i = 0; i < ELECTRODES_PER_SENSOR; i++)
    {
        float delta = baseline[BUTTON_SENSOR][i] - filtered[i];
        bool wasOn = touched & (1 << i);
        bool on = delta >= (wasOn ? BUTTON_OFF : BUTTON_ON);

        if (on)
            touched |= (1 << i);
        else
        {
            touched &= ~(1 << i);
            float error = -delta;
            baseline[BUTTON_SENSOR][i] += error * (error > NOISE_FLOOR ? BASELINE_RATE_RISING : BASELINE_RATE);
        }
#ifdef MOTUS_DEBUG
        if (delta > NOISE_FLOOR)
            anyActive = true;
#endif
    }

#ifdef MOTUS_DEBUG
    // B,<ms>,<12 electrode deltas>,<touched bits>
    if (anyActive)
    {
        Serial.printf("B,%lu", millis());
        for (int i = 0; i < ELECTRODES_PER_SENSOR; i++)
            Serial.printf(",%d", (int)lroundf(baseline[BUTTON_SENSOR][i] - filtered[i]));
        Serial.printf(",%u\n", touched);
    }
#endif
    return touched;
}
