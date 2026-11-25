#include "pads.h"

#include <Arduino.h>

#include "modes.h"

// RATE: loops per second, exponential over the setting
static const float PAD_RATE_MIN_HZ = 0.01f;
static const float PAD_RATE_MAX_HZ = 20.0f;

// Lorenz: sigma and beta fixed, CHAOS sets rho. Below 24.74 the attractor
// gives way to two fixed points, so CHAOS starts just above: from long
// spirals with rare flips between the lobes to frequent, erratic flips.
static const float LORENZ_SIGMA = 10.0f;
static const float LORENZ_BETA = 8.0f / 3.0f;
static const float LORENZ_RHO_MIN = 24.8f;
static const float LORENZ_RHO_SPAN = 35.0f; // to 59.8, along CHAOS squared
static const float LORENZ_LOOP = 0.66f;     // model time of one turn around a lobe

// Rössler: a and b fixed, CHAOS sets c: a single loop at 2.5, period
// doubling up to about 4.2, then chaos (5.7 is the classic one)
static const float ROSSLER_A = 0.2f;
static const float ROSSLER_B = 0.2f;
static const float ROSSLER_C_MIN = 2.5f;
static const float ROSSLER_C_SPAN = 6.5f; // to 9
static const float ROSSLER_LOOP = 6.1f;   // model time of one loop

// The finger pulls in two ways. A spring, against the flow and so the same at
// any rate: a light touch nudges the trajectory, a firmer one bends it toward
// the finger. And from a firm press on, the outputs are drawn to the finger in
// real time, so the firmest press pins them under it, however fast the flow.
static const float LORENZ_PULL = 200.0f;  // the spring at the firmest press, 1/model time
static const float ROSSLER_PULL = 20.0f;
static const float PULL_LIGHTEST = 0.05f; // of the full spring, at the lightest touch
static const float PIN_FROM = 0.5f;       // pressure (0-1) where pinning begins
static const float PIN_RATE = 2000.0f;    // 1/s at the firmest press: within a millisecond or two

// integration: RK4 steps no longer than this (model time), at most this many per call
static const float LORENZ_STEP = 0.004f;
static const float ROSSLER_STEP = 0.03f;
static const int MAX_STEPS = 32;
static const float MAX_DT = 0.05f; // real seconds: a longer gap (a slow save) does not jump ahead

PadSettings padSettings[NUM_PADS];

struct Attractor
{
    float v[3];        // x, y, z in the model's own units
    uint8_t mode;      // the mode the state belongs to
    bool touched;
    float targetX, targetY; // the finger, 0-1 in output coordinates
    float pull;             // the spring, 0-1
    float pin;              // pinning, 0-1
    uint32_t lastUs;
};
static Attractor attractor[NUM_PADS];

void padsDefaults()
{
    // xy, 0-8.17 V on both axes, unquantized, no slew
    int full = MAX_CENTS / PAD_CENTS_STEP * PAD_CENTS_STEP;
    for (int pad = 0; pad < NUM_PADS; pad++)
        padSettings[pad] = {PAD_XY, 50, 50, 0, 0, PAD_AXIS_X | PAD_AXIS_Y, 0, 0, (int16_t)full, 0, (int16_t)full};
}

bool padsRunning(int pad)
{
    return padSettings[pad].mode != PAD_XY;
}

float padRateHz(int rate)
{
    return PAD_RATE_MIN_HZ * powf(PAD_RATE_MAX_HZ / PAD_RATE_MIN_HZ, constrain(rate, 0, PAD_RATE_STEPS) / (float)PAD_RATE_STEPS);
}

const char *padRateText(int rate)
{
    static char text[12];
    float hz = padRateHz(rate);
    char number[8];
    snprintf(number, sizeof(number), hz < 0.1f ? "%.3f" : (hz < 10 ? "%.2f" : "%.1f"), hz);
    snprintf(text, sizeof(text), "%6sHz", number);
    return text;
}

const char *padModeName(int mode)
{
    static const char *NAMES[NUM_PAD_MODES] = {"xy", "lorenz", "rossler"};
    return NAMES[mode % NUM_PAD_MODES];
}

static float chaosOf(int pad)
{
    return constrain(padSettings[pad].chaos, 0, PAD_CHAOS_MAX) / (float)PAD_CHAOS_MAX;
}

static float lorenzRho(int pad)
{
    float c = chaosOf(pad);
    return LORENZ_RHO_MIN + LORENZ_RHO_SPAN * c * c;
}

static float rosslerC(int pad)
{
    return ROSSLER_C_MIN + ROSSLER_C_SPAN * chaosOf(pad);
}

// Where each output's 0 and 1 lie, in model units, for the parameter: the
// attractor grows with it. out[i] is the output (X, Y, Z) and var[i] the
// variable it shows.
struct Frame
{
    int var[3];
    float low[3], high[3];
};

static Frame frameOf(int pad)
{
    Frame f;
    if (padSettings[pad].mode == PAD_LORENZ)
    {
        float rho = lorenzRho(pad);
        float c = sqrtf(LORENZ_BETA * (rho - 1)); // the lobes' centers lie at x = y = ±c
        float wing = (2.9f + chaosOf(pad)) * c; // y swings wider as rho grows
        f = {{0, 2, 1}, {-2.4f * c, 0.1f * (rho - 1), -wing}, {2.4f * c, 1.8f * (rho - 1), wing}};
    }
    else
    {
        float c = rosslerC(pad);
        // z: up to where nearly all spikes reach; the highest are clipped
        f = {{0, 1, 2}, {-1.65f * c, -1.95f * c, 0}, {2.05f * c, 1.42f * c, 3.0f * c}};
    }
    return f;
}

static void restart(int pad)
{
    Attractor &a = attractor[pad];
    a.mode = padSettings[pad].mode;
    if (a.mode == PAD_LORENZ)
    {
        a.v[0] = 1;
        a.v[1] = 1;
        a.v[2] = lorenzRho(pad) - 1;
    }
    else
    {
        a.v[0] = 1;
        a.v[1] = 1;
        a.v[2] = 0;
    }
}

// the flow, with the finger's pull on the two variables X and Y show
static void derivative(int mode, float p, const float *v, const float *target, const int *pulled, float k, float *d)
{
    if (mode == PAD_LORENZ)
    {
        d[0] = LORENZ_SIGMA * (v[1] - v[0]);
        d[1] = v[0] * (p - v[2]) - v[1];
        d[2] = v[0] * v[1] - LORENZ_BETA * v[2];
    }
    else
    {
        d[0] = -v[1] - v[2];
        d[1] = v[0] + ROSSLER_A * v[1];
        d[2] = ROSSLER_B + v[2] * (v[0] - p);
    }
    for (int i = 0; i < 2; i++)
        d[pulled[i]] += k * (target[i] - v[pulled[i]]);
}

static void step(int pad, float dt)
{
    Attractor &a = attractor[pad];
    const PadSettings &s = padSettings[pad];
    if (a.mode != s.mode)
        restart(pad);

    bool lorenz = s.mode == PAD_LORENZ;
    float p = lorenz ? lorenzRho(pad) : rosslerC(pad);
    float time = dt * padRateHz(s.rate) * (lorenz ? LORENZ_LOOP : ROSSLER_LOOP);
    float maxStep = lorenz ? LORENZ_STEP : ROSSLER_STEP;
    int steps = constrain((int)ceilf(time / maxStep), 1, MAX_STEPS);
    float h = time / steps;

    // the finger's place in model units, on the variables X and Y show
    Frame f = frameOf(pad);
    float target[2] = {f.low[0] + a.targetX * (f.high[0] - f.low[0]),
                       f.low[1] + a.targetY * (f.high[1] - f.low[1])};
    float k = a.touched ? a.pull * (lorenz ? LORENZ_PULL : ROSSLER_PULL) : 0;
    k = min(k, 2.0f / h); // stays stable at any rate

    for (int n = 0; n < steps; n++)
    {
        float k1[3], k2[3], k3[3], k4[3], t[3];
        derivative(s.mode, p, a.v, target, f.var, k, k1);
        for (int i = 0; i < 3; i++)
            t[i] = a.v[i] + 0.5f * h * k1[i];
        derivative(s.mode, p, t, target, f.var, k, k2);
        for (int i = 0; i < 3; i++)
            t[i] = a.v[i] + 0.5f * h * k2[i];
        derivative(s.mode, p, t, target, f.var, k, k3);
        for (int i = 0; i < 3; i++)
            t[i] = a.v[i] + h * k3[i];
        derivative(s.mode, p, t, target, f.var, k, k4);
        for (int i = 0; i < 3; i++)
            a.v[i] += h / 6 * (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]);
    }

    // pinning: the variables X and Y show are drawn to the finger
    if (a.touched && a.pin > 0)
    {
        float draw = 1 - expf(-dt * PIN_RATE * a.pin);
        for (int i = 0; i < 2; i++)
            a.v[f.var[i]] += draw * (target[i] - a.v[f.var[i]]);
    }

    // held far off the attractor, a variable can run away (Rössler's z with x
    // held above c): it stays within a few times the outputs' span, and
    // should it ever blow up, it starts again
    for (int i = 0; i < 3; i++)
    {
        float span = f.high[i] - f.low[i];
        float &v = a.v[f.var[i]];
        if (!isfinite(v))
        {
            restart(pad);
            break;
        }
        v = constrain(v, f.low[i] - 2 * span, f.high[i] + 2 * span);
    }
}

void padsUpdate()
{
    uint32_t now = micros();
    for (int pad = 0; pad < NUM_PADS; pad++)
    {
        Attractor &a = attractor[pad];
        float dt = min((now - a.lastUs) * 1e-6f, MAX_DT);
        a.lastUs = now;
        if (padsRunning(pad))
            step(pad, dt);
    }
}

void padsFinger(int pad, bool touched, int x, int y, int pressure)
{
    Attractor &a = attractor[pad];
    a.touched = touched;
    if (!touched)
        return;
    a.targetX = constrain(x, 0, 1023) / 1023.0f;
    a.targetY = constrain(y, 0, 1023) / 1023.0f;
    float press = constrain(pressure, 0, 511) / 511.0f;
    a.pull = PULL_LIGHTEST + (1 - PULL_LIGHTEST) * press * press;
    float pin = constrain((press - PIN_FROM) / (1 - PIN_FROM), 0.0f, 1.0f);
    a.pin = pin * pin;
}

void padsOutput(int pad, int &x, int &y, int &z)
{
    const Attractor &a = attractor[pad];
    Frame f = frameOf(pad);
    float out[3];
    for (int i = 0; i < 3; i++)
        out[i] = constrain((a.v[f.var[i]] - f.low[i]) / (f.high[i] - f.low[i]), 0.0f, 1.0f);
    x = lroundf(out[0] * 4095);
    y = lroundf(out[1] * 4095);
    z = lroundf(out[2] * 511);
}

// ---- the outputs: range, quantizer, slew

static const float PAD_FULL_VOLTS = 8.19f;      // the X and Y jacks at DAC value 4095
static const int NOTE_HOLD_CENTS = 20;           // a new note must be this much nearer than the last: no flicker
static const float PAD_SLEW_MAX_MS = 10000.0f;

struct Axis
{
    float volts;    // what goes out, gliding
    int note;       // the last quantized note, cents, -1 before the first
    bool started;   // volts holds a value
};
static Axis axes[NUM_PADS][2];
static uint32_t shapedUs[NUM_PADS];

static float slewMs(int slew)
{
    return slew <= 0 ? 0 : powf(PAD_SLEW_MAX_MS, (constrain(slew, 1, PAD_SLEW_STEPS) - 1) / (float)(PAD_SLEW_STEPS - 1));
}

static void axisRange(int pad, int axis, int &base, int &range)
{
    const PadSettings &s = padSettings[pad];
    base = axis ? s.yBase : s.xBase;
    range = axis ? s.yRange : s.xRange;
}

static float shapeAxis(int pad, int axis, float u, float dt)
{
    const PadSettings &s = padSettings[pad];
    Axis &a = axes[pad][axis];
    int base, range;
    axisRange(pad, axis, base, range);
    int cents = constrain(lroundf(base + constrain(u, 0.0f, 1.0f) * range), 0, MAX_CENTS);
    if (s.scale && (s.axes & (axis ? PAD_AXIS_Y : PAD_AXIS_X)))
    {
        int note = modesQuantizeCents(s.scale - 1, s.root, cents);
        if (a.started && a.note >= 0 && note != a.note && abs(cents - a.note) < abs(cents - note) + NOTE_HOLD_CENTS)
            note = a.note; // still near enough the last one
        a.note = note;
        cents = note;
    }
    else
        a.note = -1;

    float target = (float)cents / CENTS_PER_VOLT;
    float ms = slewMs(s.slew);
    if (!a.started || ms <= 0)
        a.volts = target;
    else
        a.volts += (target - a.volts) * (1 - expf(-dt * 1000 * 5 / ms)); // within 1 % after SLEW
    a.started = true;
    return a.volts;
}

void padsShape(int pad, float x, float y, int &codeX, int &codeY)
{
    uint32_t now = micros();
    float dt = min((now - shapedUs[pad]) * 1e-6f, MAX_DT);
    shapedUs[pad] = now;
    codeX = constrain(lroundf(shapeAxis(pad, 0, x, dt) / PAD_FULL_VOLTS * 4095), 0, 4095);
    codeY = constrain(lroundf(shapeAxis(pad, 1, y, dt) / PAD_FULL_VOLTS * 4095), 0, 4095);
}

float padsLevel(int pad, int axis)
{
    int base, range;
    axisRange(pad, axis, base, range);
    if (range == 0)
        return 0.5f;
    return constrain((axes[pad][axis].volts * CENTS_PER_VOLT - base) / range, 0.0f, 1.0f);
}

const char *padsValueText(int pad, int axis)
{
    static char text[2][8];
    const Axis &a = axes[pad][axis];
    if (a.note >= 0)
    {
        int semitone = a.note / SEMITONE_CENTS;
        snprintf(text[axis], sizeof(text[axis]), "%s%u", noteName(semitone), (unsigned)(semitone / 12) % 10);
    }
    else
        snprintf(text[axis], sizeof(text[axis]), "%4.2f", constrain(a.volts, 0.0f, 9.99f));
    return text[axis];
}

const char *padSlewText(int slew)
{
    static char text[9];
    float ms = slewMs(slew);
    if (ms <= 0)
        snprintf(text, sizeof(text), "%8s", "off");
    else if (ms < 999.5f)
        snprintf(text, sizeof(text), "%5u ms", (unsigned)lroundf(ms) % 1000);
    else
        snprintf(text, sizeof(text), "%6.2f s", ms / 1000);
    return text;
}

const char *padAxesName(int axes)
{
    static const char *NAMES[4] = {"none", "x", "y", "x+y"};
    return NAMES[axes & 3];
}
