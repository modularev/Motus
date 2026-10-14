#include "display.h"

#include <Arduino.h>
#include <LiquidCrystal.h>

#include "config.h"

static const uint32_t REDRAW_INTERVAL_MS = 40;  // at most 25 redraws per second
static const uint32_t RELEASE_TIMEOUT_MS = 100; // a control not reported for this long counts as released
static const uint32_t IDLE_TIMEOUT_MS = 2000;   // back to the idle screen after the last touch
static const uint32_t MESSAGE_MS = 1000;        // how long a message is shown

// screen id for var editing, after the sensor channels
static const int EDIT_SCREEN = NUM_SENSORS;

// bar graph: custom characters 1-5 have that many pixel columns filled
static const int BAR_COLUMNS_PER_CELL = 5;
static const int BAR_STEPS = LCD_COLS * BAR_COLUMNS_PER_CELL;

static LiquidCrystal lcd(LCD_RS_PIN, LCD_RW_PIN, LCD_ENABLE_PIN, LCD_D4_PIN, LCD_D5_PIN, LCD_D6_PIN, LCD_D7_PIN);

static int statusPreset = 0;
static bool statusFine = false;
static bool statusArmed = false;

static char message[LCD_COLS + 1];
static uint32_t messageUntil = 0;

// what should be on the screen and what currently is
static char wanted[LCD_ROWS][LCD_COLS + 1];
static char shown[LCD_ROWS][LCD_COLS];

static int activeChannel = -1;
static uint32_t lastSeen[NUM_SENSORS + 1];
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

void displaySlider(int channel, int position, int pressure, bool recording)
{
    if (!claimScreen(channel))
        return;

    snprintf(wanted[0], sizeof(wanted[0]), "S%u%6d", (unsigned)(channel + 1) % 10, constrain(position, 0, 1023));
    if (recording)
        wanted[0][2] = 'R';

    // position bar, or pressure bar while pressing
    if (pressure > 0)
        formatBar(wanted[1], pressure, 511);
    else
        formatBar(wanted[1], position, 1023);
}

void displayPad(int channel, int x, int y, bool recording)
{
    if (!claimScreen(channel))
        return;

    snprintf(wanted[0], sizeof(wanted[0]), "P%u X%4d", (unsigned)(channel - NUM_SLIDERS + 1) % 10, constrain(x, 0, 1023));
    if (recording)
        wanted[0][2] = 'R';
    snprintf(wanted[1], sizeof(wanted[1]), "   Y%4d", constrain(y, 0, 1023));
}

void displayEdit(int preset, int var, int value)
{
    if (!claimScreen(EDIT_SCREEN))
        return;

    // S3 V2 FR
    // ####4095
    snprintf(wanted[0], sizeof(wanted[0]), "S%u V%u %c%c", (unsigned)(preset + 1) % 10, (unsigned)(var + 1) % 10,
             statusFine ? 'F' : ' ', statusArmed ? 'R' : ' ');
    formatBar(wanted[1], value, 4095, 4);
    snprintf(wanted[1] + 4, sizeof(wanted[1]) - 4, "%4d", constrain(value, 0, 4095));
}

void displayStatus(int preset, bool fine)
{
    statusPreset = preset;
    statusFine = fine;
}

void displayRecordArmed(bool armed)
{
    statusArmed = armed;
}

void displayMessage(const char *text)
{
    snprintf(message, sizeof(message), "%-8s", text);
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
        snprintf(wanted[0], sizeof(wanted[0]), "MOTUS %c%c", statusFine ? 'F' : ' ', statusArmed ? 'R' : ' ');
        snprintf(wanted[1], sizeof(wanted[1]), "PRESET %u", (unsigned)(statusPreset + 1) % 10);
    }

    if (millis() < messageUntil)
    {
        memcpy(wanted[0], message, LCD_COLS);
        snprintf(wanted[1], sizeof(wanted[1]), "        ");
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
