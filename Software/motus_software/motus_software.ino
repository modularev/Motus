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

PCA9685 pca[4] = {
  PCA9685(0x40, PCA9685_MODE_LED_DIRECT, 800.0),
  PCA9685(0x41, PCA9685_MODE_LED_DIRECT, 800.0),
  PCA9685(0x42, PCA9685_MODE_LED_DIRECT, 800.0),
  PCA9685(0x43, PCA9685_MODE_LED_DIRECT, 800.0)
};

Adafruit_MPR121 cap = Adafruit_MPR121();
MCP23017 mcp = MCP23017(0x20);
Encoder enc(23, 22);
LiquidCrystal lcd(lcd_rs, lcd_rw, lcd_enable, lcd_d4, lcd_d5, lcd_d6, lcd_d7);

//mpr data init
int stored_data[12] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };
int max_index = 0;
int max_value = 0;
int centroid = 0;
int touchState = 0;

//encoder data
int current_value;
int previous_value;

int led_index[48] = {
  5, 4, 3, 2, 1, 0,
  11, 10, 9, 8, 7, 6,
  1, 0, 15, 14, 13, 12,
  7, 6, 5, 4, 3, 2,
  13, 12, 11, 10, 9, 8,
  3, 2, 1, 0, 15, 14,
  9, 8, 7, 6, 5, 4,
  15, 14, 13, 12, 11, 10
};

int pca_index[48] = {
  0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0,
  1, 1, 0, 0, 0, 0,
  1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 
  2, 2, 2, 2, 1, 1,
  2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2
};

void setup() {

  Wire.setClock(100000);
  Wire.begin();

  Serial.begin(9600);

  //initialize MCP23018 GPIO Expander as 16 outputs
  mcp.init();
  mcp.portMode(MCP23017_PORT::A, 0);
  mcp.portMode(MCP23017_PORT::B, 0);

  //set ADDR pins on MPR121 HIGH
  for (int i = 0; i < 11; i++) {
    mcp.digitalWrite(i, HIGH);
  }

  //Default address is 0x5A, if tied to 3.3V its 0x5B
  if (!cap.begin(0x5B)) {
    Serial.println("MPR121 not found, check wiring?");
    while (1);
  }
  cap.begin(0x5A);

  delay(10);
 
  //initialize LED drivers
  for (int i = 0; i < 4; i++) {
    
    pca[i].setup();

    for (int j = 0; j < 16; j++){
      pca[i].getPin(j).fullOffAndWrite();
    } 
  }

  //Serial.println("MPR121 found!");

  //set up the LCD's number of columns and rows:
  lcd.begin(8, 2);
  //Print a message to the LCD.
  //lcd.autoscroll();
  lcd.print("hello");

}

/////////check for touched pad/////////
/*
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
}*/

void mpr_read(int mpr_channel) {

  mcp.digitalWrite(mpr_channel, LOW);
  touchState = cap.touched();

  if (touchState) {
    max_value = 0;
    for (int i = 0 ; i < 12 ; i++) {
      stored_data[i] = (900 - cap.filteredData(i));
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

    slider_led(mpr_channel, centroid);
    //Serial.println(centroid);/*
    lcd.setCursor(4, 1);
    lcd.print(F("    "));
    lcd.setCursor(4, 1);
    lcd.print(centroid);
    lcd.setCursor(0, 1);
    lcd.print(F("CH  "));
    lcd.setCursor(2, 1);
    lcd.print(mpr_channel);

    usbMIDI.sendControlChange(mpr_channel, centroid / 8, 1);
  }

  mcp.digitalWrite(mpr_channel, HIGH);
}

void slider_led(int slider_channel, int value) {
  
  float divValue = value / 170.0; //10bit value divided by 6 (1024 / 170.66 )
  int fullLed = (int)divValue;
  float frac = divValue - fullLed;
  int dimLed = frac * (float)4095; //scale fractional part to 8bit PWM value
  int ledStart = slider_channel * 6;
  int ledEnd = ledStart + fullLed;

  //clear slider leds
  for (int pos = ledStart ; pos < (ledStart + 6) ; pos++){
      pca[pca_index[pos]].getPin(led_index[pos]).fullOffAndWrite();
    }

  //write full slider leds
  for (int pos = ledStart ; pos < ledEnd + 1 ; pos++){
    pca[pca_index[pos]].getPin(led_index[pos]).fullOnAndWrite();
  }

  //write dim slider leds
  pca[pca_index[ledEnd + 1]].getPin(led_index[ledEnd + 1]).setValueAndWrite(dimLed);
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
  //delay(10);
  for (int i = 0; i < 8; i++) {
    mpr_read(i);
  }
}
