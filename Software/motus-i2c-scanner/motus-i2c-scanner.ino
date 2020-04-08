//MPR121 test program

#include <Wire.h>
#include "MCP23017.h"
#include "Adafruit_MPR121.h"

#ifndef _BV
#define _BV(bit) (1 << (bit))
#endif

Adafruit_MPR121 mpr = Adafruit_MPR121();
MCP23017 mcp = MCP23017(0x20);

void setup() {
  //set i2c Speed to HighSpeed (400kHz)
  Wire.setClock(400000);
  Wire.begin();
  Serial.begin(115200);
  while (!Serial) delay(1);
  Serial.println("\nMCP/MPR check");

  //initialize MCP23018 GPIO Expander as 16 outputs
  mcp.init();
  mcp.portMode(MCP23017_PORT::A, 0);
  mcp.portMode(MCP23017_PORT::B, 0);
  
  //set ADDR pins on MPR121 HIGH
  for (int i = 0; i < 11; i++) {

    mcp.digitalWrite(i, HIGH);
  }
}

void checkmcp() {
  byte error;

  Wire.beginTransmission(0x20);
  error = Wire.endTransmission();
  if (error == 0) {
    Serial.print("\ni2c on MCP23018 \tworks!");
  }
  else {
    Serial.print("\n\t\t\tMCP23018 is not working :)");
  }
}

void checkmpr() {
  byte error;

  for (int i = 0; i < 14; i++) {
    mcp.digitalWrite(i, LOW);
    /*
      if (!mpr.begin(0x5A)) {
      Serial.print("\nI2c on MPR #");
      Serial.print(i + 1);
      Serial.print(" is not working :(");
      }
      else if (mpr.begin(0x5A)) {
      Serial.print("\nI2c on MPR #");
      Serial.print(i + 1);
      Serial.print(" works!");
      }*/

    Wire.beginTransmission(90);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("\ni2c on MPR121 #");
      Serial.print(i + 1);
      Serial.print("\tworks!");
    }
    else {
      Serial.print("\ni2c on MPR121 #");
      Serial.print(i + 1);
      Serial.print("\tis not working :)");
    }
    mcp.digitalWrite(i, HIGH);
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  Serial.print("\n\n\n");

  checkmcp();
  checkmpr();
  delay(1000);
}
