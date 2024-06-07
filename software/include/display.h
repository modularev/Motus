#ifndef DISPLAY_H
#define DISPLAY_H

#include <LiquidCrystal.h>
#include <Encoder.h>
#include "zlookup.h"

// LCD pin definition
#define lcd_rs 5
#define lcd_rw 7
#define lcd_enable 6
#define lcd_d4 0
#define lcd_d5 1
#define lcd_d6 2
#define lcd_d7 3

extern Encoder enc;
extern LiquidCrystal lcd;

extern uint16_t centroid;
extern uint16_t x_centroid;
extern uint16_t y_centroid;

extern uint16_t current_value;
extern uint16_t previous_value;

void display_setup();
void display_value_xy(int mpr_channel, int x_centroid, int y_centroid, int pressure);
void display_value(int mpr_channel, int centroid, int pressure);
void enc_update();

#endif
