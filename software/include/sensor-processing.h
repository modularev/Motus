#ifndef SENSOR_PROCESSING_H
#define SENSOR_PROCESSING_H

#include <Adafruit_MPR121.h>
#include <MCP23017.h>

#include "led.h"
#include "analogout.h"
#include "display.h"

// for mpr
#ifndef _BV
#define _BV(bit) (1 << (bit))
#endif

void sensorSetup();
void sensorCheck();
int mpr_read(int start, int count);
void sensorRead(int sensorChan);
int getPress(int16_t touchStrength[]);
float getInterpolatedPosition(int16_t touchStrength[], uint8_t length);
uint16_t check_upper(int index);
uint16_t check_upper_x(int index);
uint16_t check_lower(int index);
uint16_t check_lower_y(int index);

extern Adafruit_MPR121 cap;
extern MCP23017 mcp;


const int xy_access_array[12] = {0, 1, 2, 3, 4, 5, 6, 7, 11, 10, 9, 8};

// mpr data init
extern uint16_t stored_data[];
extern int8_t max_index;
extern uint16_t max_value;
extern uint16_t centroid;
extern uint16_t x_centroid;
extern uint16_t y_centroid;
extern uint16_t pressure;
extern uint16_t xy_pressure;
extern int touchState;
extern bool touchcheck;

#endif
