#include <Keypad.h>

void setup() {
  // put your setup code here, to run once:
  // Keyswitch Matrix Definition:
  const byte ROWS = 4;
  const byte COLS = 8;

  char keys[ROWS][COLS] = {
    {'1','2','3','4','5','6','7','8'},
    {'9','A','B','C','D','E','F','G'},
    {'H','I','J','K','L','M','N','O'},
    {'P','Q','R','S','T','U','V','W'}
  }; 

  byte rowPins[ROWS]={0,1,2,3};
  byte colPins[COLS]={4,5,6,7,8,9,10,11};

  Keypad kpd = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
}

void loop() {
  // put your main code here, to run repeatedly:

}
