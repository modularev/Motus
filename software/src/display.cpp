#include "display.h"

#include <Arduino.h>
#include <LiquidCrystal.h>

#include "config.h"

static const uint32_t REDRAW_INTERVAL_MS = 40;  // at most 25 redraws per second
static const uint32_t RELEASE_TIMEOUT_MS = 100; // a control not reported for this long counts as released
static const uint32_t IDLE_TIMEOUT_MS = 2000;   // back to the idle screen after the last touch
static const uint32_t MESSAGE_MS = 1000;        // how long a message is shown

static const uint32_t MENU_HOLD_MS = 200;      // the menu screen stays while displayMenu keeps being called

// bar graph: custom characters 1-5 have that many pixel columns filled
static const int BAR_COLUMNS_PER_CELL = 5;
static const int BAR_STEPS = LCD_COLS * BAR_COLUMNS_PER_CELL;

static LiquidCrystal lcd(LCD_RS_PIN, LCD_RW_PIN, LCD_ENABLE_PIN, LCD_D4_PIN, LCD_D5_PIN, LCD_D6_PIN, LCD_D7_PIN);

static int statusProgress[NUM_VARS];
static int statusSlot = 0;
static bool statusRunning = false;
static char statusTempo[4];
static bool statusUnsaved = false;
static bool statusArmed = false;
static bool statusRecording = false;

static const uint32_t ARMED_BLINK_MS = 400; // the recording dot blinks while armed

static char message[LCD_ROWS][LCD_COLS + 1];
static char menu[LCD_ROWS][LCD_COLS + 1];
static uint32_t menuUntil = 0;
static char progress[LCD_ROWS][LCD_COLS + 1];
static uint32_t progressUntil = 0;
static uint32_t messageUntil = 0;

// what should be on the screen and what currently is
static char wanted[LCD_ROWS][LCD_COLS + 1];
static char shown[LCD_ROWS][LCD_COLS];

static int activeChannel = -1;
static uint32_t lastSeen[NUM_SENSORS];
static elapsedMillis sinceRedraw;

void displaySetup()
{
    lcd.begin(LCD_COLS, LCD_ROWS);

    for (int columns = 1; columns <= BAR_COLUMNS_PER_CELL; columns++)
    {
        uint8_t glyph[8];
        memset(glyph, (0x1F << (BAR_COLUMNS_PER_CELL - columns)) & 0x1F, sizeof(glyph));
        lcd.createChar(columns, glyph);
    }

    // recording dot and FINE indicator (plus-minus)
    uint8_t dot[8] = {0b00000, 0b01110, 0b11111, 0b11111, 0b11111, 0b01110, 0b00000, 0b00000};
    uint8_t fineGlyph[8] = {0b00100, 0b01110, 0b00100, 0b00000, 0b01110, 0b00000, 0b00000, 0b00000};
    lcd.createChar(GLYPH_DOT, dot);
    lcd.createChar(GLYPH_FINE, fineGlyph);
    lcd.clear();
}

// a new touch takes the screen, and keeps it until it is released
static bool claimScreen(int channel)
{
    uint32_t now = millis();
    bool newTouch = now - lastSeen[channel] > RELEASE_TIMEOUT_MS;
    bool activeReleased = activeChannel < 0 || now - lastSeen[activeChannel] > RELEASE_TIMEOUT_MS;
    lastSeen[channel] = now;

    if (newTouch || activeReleased)
        activeChannel = channel;
    return activeChannel == channel;
}

static void formatBar(char *row, int value, int maxValue, int cells = LCD_COLS)
{
    int steps = cells * BAR_COLUMNS_PER_CELL;
    int level = constrain((long)value * steps / maxValue, 0, steps);
    for (int col = 0; col < cells; col++)
    {
        int columns = constrain(level - col * BAR_COLUMNS_PER_CELL, 0, BAR_COLUMNS_PER_CELL);
        row[col] = columns ? columns : ' ';
    }
}

void displaySlider(int channel, int position, int pressure, bool recording, const char *text)
{
    if (!claimScreen(channel))
        return;

    // the value only: the touched slider is where the finger is
    if (text[0])
        snprintf(wanted[0], sizeof(wanted[0]), "%8s", text);
    else
        snprintf(wanted[0], sizeof(wanted[0]), "%8d", constrain(position, 0, 1023));
    if (recording)
        wanted[0][0] = GLYPH_DOT;

    // position bar, or pressure bar while pressing
    if (pressure > 0)
        formatBar(wanted[1], pressure, 511);
    else
        formatBar(wanted[1], position, 1023);
}

void displaySliderText(int channel, const char *top, const char *bottom)
{
    if (!claimScreen(channel))
        return;
    snprintf(wanted[0], sizeof(wanted[0]), "%-8s", top);
    snprintf(wanted[1], sizeof(wanted[1]), "%-8s", bottom);
}

void displayPad(int channel, int x, int y, bool recording)
{
    if (!claimScreen(channel))
        return;

    snprintf(wanted[0], sizeof(wanted[0]), "P%u X%4d", (unsigned)(channel - NUM_SLIDERS + 1) % 10, constrain(x, 0, 1023));
    if (recording)
        wanted[0][2] = GLYPH_DOT;
    snprintf(wanted[1], sizeof(wanted[1]), "   Y%4d", constrain(y, 0, 1023));
}

void displayProgress(const char *top, float fraction)
{
    snprintf(progress[0], sizeof(progress[0]), "%-8s", top);
    formatBar(progress[1], lroundf(constrain(fraction, 0.0f, 1.0f) * 1000), 1000);
    progressUntil = millis() + MENU_HOLD_MS;
}

void displayMenu(const char *top, const char *bottom)
{
    snprintf(menu[0], sizeof(menu[0]), "%-8s", top);
    snprintf(menu[1], sizeof(menu[1]), "%-8s", bottom);
    menuUntil = millis() + MENU_HOLD_MS;
}

void displayStatus(const int progress[], bool running, const char *tempo, int slot, bool unsaved)
{
    memcpy(statusProgress, progress, sizeof(statusProgress));
    statusSlot = slot;
    statusRunning = running;
    snprintf(statusTempo, sizeof(statusTempo), "%s", tempo);
    statusUnsaved = unsaved;
}

void displayRecordState(bool armed, bool recording)
{
    statusArmed = armed;
    statusRecording = recording;
}

void displayMessage(const char *text, const char *second)
{
    snprintf(message[0], sizeof(message[0]), "%-8s", text);
    snprintf(message[1], sizeof(message[1]), "%-8s", second);
    messageUntil = millis() + MESSAGE_MS;
}

void displayUpdate()
{
    if (sinceRedraw < REDRAW_INTERVAL_MS)
        return;
    sinceRedraw = 0;

    if (activeChannel < 0 || millis() - lastSeen[activeChannel] > IDLE_TIMEOUT_MS)
    {
        activeChannel = -1;
        // running: RUN 120● over a bar per row filling up through its length, and *
        // stopped:  STOP 120 over Slot 2●*      (S3 instead of 120 with a slider clock)
        bool dot = statusRecording || (statusArmed && (millis() / ARMED_BLINK_MS) % 2 == 0);
        char unsaved = statusUnsaved ? '*' : ' ';
        if (statusRunning)
        {
            snprintf(wanted[0], sizeof(wanted[0]), "RUN %3s%c", statusTempo, dot ? GLYPH_DOT : ' ');
            for (int row = 0; row < NUM_VARS; row++)
            {
                wanted[1][2 * row] = constrain(statusProgress[row], 1, BAR_COLUMNS_PER_CELL); // bar glyphs 1-5
                wanted[1][2 * row + 1] = ' ';
            }
            wanted[1][LCD_COLS - 1] = unsaved;
            wanted[1][LCD_COLS] = 0;
        }
        else
        {
            snprintf(wanted[0], sizeof(wanted[0]), "STOP %3s", statusTempo);
            snprintf(wanted[1], sizeof(wanted[1]), "Slot %u%c%c", (unsigned)(statusSlot + 1) % 10, dot ? GLYPH_DOT : ' ', unsaved);
        }
    }

    if (millis() < menuUntil)
    {
        memcpy(wanted[0], menu[0], LCD_COLS);
        memcpy(wanted[1], menu[1], LCD_COLS);
    }

    if (millis() < progressUntil)
    {
        memcpy(wanted[0], progress[0], LCD_COLS);
        memcpy(wanted[1], progress[1], LCD_COLS);
    }

    if (millis() < messageUntil)
    {
        memcpy(wanted[0], message[0], LCD_COLS);
        memcpy(wanted[1], message[1], LCD_COLS);
    }

    // write only the characters that changed
    for (int row = 0; row < LCD_ROWS; row++)
    {
        bool cursorSet = false;
        for (int col = 0; col < LCD_COLS; col++)
        {
            if (wanted[row][col] == shown[row][col])
            {
                cursorSet = false;
                continue;
            }
            if (!cursorSet)
            {
                lcd.setCursor(col, row);
                cursorSet = true;
            }
            lcd.write((uint8_t)wanted[row][col]);
            shown[row][col] = wanted[row][col];
        }
    }
}
