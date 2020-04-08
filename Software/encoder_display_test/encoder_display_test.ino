#include <Encoder.h>
#include <LiquidCrystal.h>

// initialize the library with the numbers of the interface pins

#define lcd_rs 5
#define lcd_rw 7
#define lcd_enable 6
#define lcd_d4 0
#define lcd_d5 1
#define lcd_d6 2
#define lcd_d7 3

LiquidCrystal lcd(lcd_rs, lcd_rw, lcd_enable, lcd_d4, lcd_d5, lcd_d6, lcd_d7);
int current_value;
int previous_value;

Encoder enc(23, 22);

void setup() {

  // set up the LCD's number of columns and rows:
  lcd.begin(8, 2);
  // Print a message to the LCD.
  //lcd.autoscroll();
  lcd.print("henlo :)");
}

void loop() {
  // set the cursor to column 0, line 1
  // (note: line 1 is the second row, since counting begins with 0):
  current_value = enc.read();

  if (current_value != previous_value) {
    lcd.setCursor(0, 1);
    lcd.print(F("        "));
    lcd.setCursor(0, 1);
    lcd.print(current_value);
    previous_value = current_value;

    if (current_value < 0) {
      current_value = 0;
      enc.write(0);
    }

  }

}
