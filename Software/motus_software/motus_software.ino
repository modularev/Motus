#include <Encoder.h>
#include <LiquidCrystal.h>
#include <Wire.h>
#include "Adafruit_MPR121.h"
#include "MCP23017.h"
#include "PCA9685.h"

// LCD pin definition
#define lcd_rs 5
#define lcd_rw 7
#define lcd_enable 6
#define lcd_d4 0
#define lcd_d5 1
#define lcd_d6 2
#define lcd_d7 3

// for mpr
#ifndef _BV
#define _BV(bit) (1 << (bit))
#endif

PCA9685 pca_0(0x40);
PCA9685 pca_1(0x41);
PCA9685 pca_2(0x42);
PCA9685 pca_3(0x43);

Adafruit_MPR121 cap = Adafruit_MPR121();
MCP23017 mcp = MCP23017(0x20);
Encoder enc(23, 22);
LiquidCrystal lcd(lcd_rs, lcd_rw, lcd_enable, lcd_d4, lcd_d5, lcd_d6, lcd_d7);

//mpr data init
int stored_data[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int max_index = 0;
int max_value = 0;
float centroid = 0;
int touchState = 0;

//encoder data
int current_value;
int previous_value;


void setup() {
  Wire.setClock(400000);
  Wire.begin();

  Serial.begin(115200);

  //initialize MCP23018 GPIO Expander as 16 outputs
  mcp.init();
  mcp.portMode(MCP23017_PORT::A, 0);
  mcp.portMode(MCP23017_PORT::B, 0);

  //set ADDR pins on MPR121 HIGH
  for (int i = 0; i < 11; i++) {
    mcp.digitalWrite(i, HIGH);
  }

  // Default address is 0x5A, if tied to 3.3V its 0x5B
  if (!cap.begin(0x5B)) {
    Serial.println("MPR121 not found, check wiring?");
    while (1);
  }
  cap.begin(0x5A);
  delay(10);
  //Serial.println("MPR121 found!");

  // set up the LCD's number of columns and rows:
  lcd.begin(8, 2);
  // Print a message to the LCD.
  //lcd.autoscroll();
  lcd.print("henlo :)");

  pca_0.begin();
  pca_1.begin();
  pca_2.begin();
  pca_0.setFrequency(200);
  pca_1.setFrequency(200);
  pca_2.setFrequency(200);
  delay(100);
  pca_0.setChannels8bit(0, 15, 0);
  pca_1.setChannels8bit(0, 15, 0);
  pca_2.setChannels8bit(0, 15, 0);

}

void slider_led(int slider_channel, int value) {
  int ledsFullyOn;
  float ledDimValue;
  int ledStart;
  int newLedCh;

  ledsFullyOn = value / 170;
  ledDimValue = 1.5 * (float) (value % 170);

  if (slider_channel == 0) {
    ledStart = 0;
    pca_0.setChannels8bit(ledStart, ledsFullyOn, 255);
    pca_0.setChannels8bit(ledStart, ledsFullyOn + 1, (int)ledDimValue);
  }

  else if (slider_channel == 1) {
    ledStart = 6;
    pca_0.setChannels8bit(ledStart, ledsFullyOn, 255);
    pca_0.setChannels8bit(ledStart, ledsFullyOn + 1, (int)ledDimValue);
  }

  else if (slider_channel == 2) {
    ledStart = 12;
    pca_0.setChannels8bit(ledStart, ledsFullyOn, 255);
    pca_0.setChannels8bit(ledStart, ledsFullyOn + 1, (int)ledDimValue);

    newLedCh = (ledStart + ledsFullyOn) % 16;

    if (newLedCh < 6) {
      pca_1.setChannels8bit(0, newLedCh, 255);
      pca_1.setChannels8bit(0, newLedCh + 1, (int)ledDimValue);
    }
  }

  else if (slider_channel == 3) {
    ledStart = 2;
    pca_1.setChannels8bit(ledStart, ledsFullyOn, 255);
    pca_1.setChannels8bit(ledStart, ledsFullyOn + 1, (int)ledDimValue);
  }

  else if (slider_channel == 4) {
    ledStart = 8;
    pca_1.setChannels8bit(ledStart, ledsFullyOn, 255);
    pca_1.setChannels8bit(ledStart, ledsFullyOn + 1, (int)ledDimValue);
  }

  else if (slider_channel == 5) {
    ledStart = 14;
    pca_1.setChannels8bit(ledStart, ledsFullyOn, 255);
    pca_1.setChannels8bit(ledStart, ledsFullyOn + 1, (int)ledDimValue);

    newLedCh = (ledStart + ledsFullyOn) % 16;

    if (newLedCh < 6) {
      pca_2.setChannels8bit(0, newLedCh, 255);
      pca_2.setChannels8bit(0, newLedCh + 1, (int)ledDimValue);
    }
  }

  else if (slider_channel == 6) {
    ledStart = 4;
    pca_2.setChannels8bit(ledStart, ledsFullyOn, 255);
    pca_2.setChannels8bit(ledStart, ledsFullyOn + 1, (int)ledDimValue);
  }

  else if (slider_channel == 7) {
    ledStart = 10;
    pca_2.setChannels8bit(ledStart, ledsFullyOn, 255);
    pca_2.setChannels8bit(ledStart, ledsFullyOn + 1, (int)ledDimValue);
  }

}

void mpr_read1(int mpr_channel) {
  mcp.digitalWrite(mpr_channel, LOW);
  touchState = cap.touched();
  if (touchState) {
    lcd.setCursor(0, 0);
    lcd.print(F("        "));
    lcd.setCursor(0, 0);
    lcd.print(touchState, HEX);
    lcd.setCursor(0, 1);
    lcd.print(F("    "));
    lcd.setCursor(0, 1);
    lcd.print(mpr_channel);
  }
  mcp.digitalWrite(mpr_channel, HIGH);
}

void mpr_read(int mpr_channel) {
  mcp.digitalWrite(mpr_channel, LOW);
  touchState = cap.touched();

  if (touchState) {
    max_value = 0;
    for (int i = 0 ; i < 12 ; i++) {
      stored_data[i] = (1024 - cap.filteredData(i));
      //check maximum signal segment
      if (stored_data[i] > max_value) {
        max_value = stored_data[i];
        max_index = i;
      }
    }

    centroid =  ( ( (float) (stored_data[max_index + 1] - stored_data[max_index - 1])
                    / (float) (stored_data[max_index - 1] + stored_data[max_index] + stored_data[max_index + 1]) )
                  + max_index )
                * 85.0;//( 1024.0 / (12.0 - 1.0));

    slider_led(mpr_channel, (int) centroid);
    //Serial.println(centroid);/*
    lcd.setCursor(4, 1);
    lcd.print(F("    "));
    lcd.setCursor(4, 1);
    lcd.print((int)centroid);
    lcd.setCursor(0, 1);
    lcd.print(F("CH  "));
    lcd.setCursor(2, 1);
    lcd.print(mpr_channel);
  }

  mcp.digitalWrite(mpr_channel, HIGH);
}

void enc_update() {
  current_value = enc.read();
  if (current_value != previous_value) {
    lcd.setCursor(6, 0);
    lcd.print(F("  "));
    lcd.setCursor(6, 0);
    lcd.print(current_value);

    previous_value = current_value;

    if (current_value < 0) {
      current_value = 0;
      enc.write(0);
    }

  }
}

void loop() {
  enc_update();
  delay(10);
  for (int i = 0; i < 8; i++) {
    mpr_read(i);
  }
}
