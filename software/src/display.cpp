#include "display.h"

uint16_t current_value = 0;
uint16_t previous_value;

Encoder enc(23, 22);
LiquidCrystal lcd(lcd_rs, lcd_rw, lcd_enable, lcd_d4, lcd_d5, lcd_d6, lcd_d7);


void display_setup()
{
    lcd.begin(8, 2);
    lcd.clear();
}

void display_value_xy(int mpr_channel, int x_centroid, int y_centroid, int pressure)
{
    int pad_select = 0;
    if (mpr_channel == 8)
        pad_select = 1;
    else if (mpr_channel == 9)
        pad_select = 2;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("PAD"));
    lcd.print(pad_select);
    lcd.setCursor(5, 0);
    lcd.print(sig_lookup[pressure]); //(lower + max_value + lower) / 11);
    lcd.setCursor(0, 1);
    lcd.print(F("X"));
    lcd.print(x_centroid);
    lcd.print(F("Y"));
    lcd.print(y_centroid);
}

void display_value(int mpr_channel, int centroid, int pressure)
{
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(pressure); //(lower + max_value + lower) / 11);
    lcd.setCursor(4, 1);
    lcd.print(centroid);
    lcd.setCursor(0, 1);
    lcd.print(F("CH  "));
    lcd.setCursor(2, 1);
    lcd.print(mpr_channel + 1);
}

void enc_update()
{
    // read encoder
    current_value = enc.read();
    if (current_value > 10000)
    {
        current_value = 0;
        enc.write(0);
    }

    // check for change
    if (current_value != previous_value)
    {
        lcd.setCursor(6, 0);
        lcd.print(F("  "));
        lcd.setCursor(6, 0);
        lcd.print(current_value);
        previous_value = current_value;
    }
}
