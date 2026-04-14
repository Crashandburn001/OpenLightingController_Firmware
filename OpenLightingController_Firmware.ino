//    .------..------..------.
//    |O.--. ||L.--. ||C.--. |
//    | :/\: || :/\: || :/\: |
//    | :\/: || (__) || :\/: |
//    | '--'O|| '--'L|| '--'C|
//    `------'`------'`------'
//    Welcome to the OpenLightingControllerFirmware by L. White.
//    Last modified 12/04/2026 (DD/MM/YYYY) at 2:29PM AEST
//    Designed for usage with OpenLightingController Project by L.White, 
//    a customizable and open-source lighting control board designed to interface with dot2 by MA Lighting.


//---------------------------
//  Welcome to the Library
//---------------------------
#include <Keypad.h>
#include <EEPROM.h>

//---------------------------
//    Hardware Definitions
//---------------------------

// Matrix Settings:
const byte ROWS = 4; //The number of rows in your matrix.
const byte COLS = 8; //The number of columns in your matrix.

// Map out the physical keyswitch grid to real characters.
char keys[ROWS][COLS] = {
  { 0,  1,  2,  3,  4,  5,  6,  7},
  { 8,  9, 10, 11, 12, 13, 14, 15},
  {16, 17, 18, 19, 20, 21, 22, 23},
  {24, 25, 26, 27, 28, 29, 30, 31}
};
byte rowPins[ROWS] = {3,2,1,0}; //MATCH TO PCB
byte colPins[COLS] = {32,31,39,9,8,7,5,4};
Keypad kpd = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

const int PAGE_BUTTON = 29; //The button dedicated to changing pages.
const int NUM_PAGES = 2; //The number of pages. Less is more!
int currentPage = 0;

//Fader settings
const int numFaders = 10;
const int faderPins[numFaders] = {A14, A15, A2, A3, A4, A5, A6, A7, A8, A9}; //Change this!!!
const int FaderThreshold = 2;
int lastSentValue[numFaders];
const int deadZone = 4;


// Soft Takeover Engine
int virtualFaders[NUM_PAGES][numFaders]; //Stores the fader value for each page.
int lastPhysicalPos[numFaders];          // Tracks where the physical fader actually is
bool faderLocked[numFaders];             //Tracks whether the fader is waiting to catch up.

//Status LED
const int ledPin=13;

//---------------------------
//  State and Memory Logic
//---------------------------

uint8_t buttonGroups[NUM_PAGES][32];

//---------------------------
//      Intitialization
//---------------------------
void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  kpd.addEventListener(keypadEvent);
  analogReadResolution(10);

  //Init Fader Engines
  for(int p=0; p < NUM_PAGES; p++) {
    for(int f = 0; f < numFaders; f++) {
      virtualFaders[p][f] = 0;
    }
  }

  for(int f=0; f < numFaders; f++) {
    lastPhysicalPos[f] = analogRead(faderPins[f]) / 8;
    faderLocked[f] = false;
  }

  //Retrieve Group Logic from EEPROM
  EEPROM.get(0, buttonGroups);
  if (buttonGroups[0][0] == 255) {//Reset if blank
    for(int p=0; p<NUM_PAGES; p++) {
      for(int i=0; i<32; i++) buttonGroups[p][i] = 0;
    }
  }
  blinkPageLED();


  Serial.println("====Status Report====");
  for (int p = 0; p < NUM_PAGES; p++) {
    Serial.print("Page "); Serial.print(p+1);
    Serial.print(": Standard Midi Ch"); Serial.println (p+1);
  }
  Serial.println("Sticky Controls (Master/B.O./Tempo): MIDI Ch 1");
  Serial.println("-------------------------------------");

  // --- MIDI Configuration Report ---
  Serial.println("\n====Midi Info====");
  Serial.println("Buttons 0-29: MIDI Notes 0-29");
  Serial.println("Button 30 (Sticky): MIDI Note 30, Ch 1");
  Serial.println("Button 31 (Sticky): MIDI Note 31, Ch 1");
  Serial.println("Faders 0-7: MIDI CC 1-8");
  Serial.println("Faders 8-9 (Sticky): MIDI CC 9-10, Ch 1");
  Serial.println("-------------------------------------");
}

//---------------------------
//      Main Engine
//---------------------------

void loop() { 
  kpd.getKey();
  readFaders();
  if (Serial.available() > 0) processCLI();
  while(usbMIDI.read()) {}
}

//---------------------------
// Fader Logic (soft takeover engine)
//---------------------------

void readFaders() {
  for (int i = 0; i < numFaders; i++) {
    bool isGlobal = (i == 8 || i == 9);
    int activePage = isGlobal ? 0 : currentPage; 
    
    int currentPhysicalPos = analogRead(faderPins[i]) / 8;
    int targetVirtualPos = virtualFaders[activePage][i]; // Use activePage here

    // Apply deadzone
    if (currentPhysicalPos < deadZone) currentPhysicalPos = 0;
    if (currentPhysicalPos > (127 - deadZone)) currentPhysicalPos = 127;

    // Check if fader can be unlocked (Soft Takeover)
    if (faderLocked[i]) {
      if ((lastPhysicalPos[i] <= targetVirtualPos && currentPhysicalPos >= targetVirtualPos) ||
          (lastPhysicalPos[i] >= targetVirtualPos && currentPhysicalPos <= targetVirtualPos)) {
        faderLocked[i] = false; 
      }
    }

    // Send MIDI if unlocked
    if (!faderLocked[i] && abs(currentPhysicalPos - targetVirtualPos) >= FaderThreshold) {
      usbMIDI.sendControlChange(i + 1, currentPhysicalPos, activePage + 1);
      virtualFaders[activePage][i] = currentPhysicalPos; 
    }
    lastPhysicalPos[i] = currentPhysicalPos; //Remember this for the next loop
  }
}

//---------------------------
// Matrix Logic (including paging + interlocks)
//---------------------------

void keypadEvent(KeypadEvent key){
  int noteNumber = (int)key;
  KeyState state = kpd.getState(); // Store the state of the key

  if (state == PRESSED) {
    if (noteNumber == PAGE_BUTTON) {
      currentPage++;
      if (currentPage >= NUM_PAGES) currentPage = 0;
      for (int f=0; f < numFaders; f++) faderLocked[f] = true;
      blinkPageLED();
      return; 
    }

    uint8_t myGroup = buttonGroups[currentPage][noteNumber];
    int midiChannel = (noteNumber == 31 || noteNumber == 30) ? 1 : currentPage + 1;

    // Interlock logic
    if (myGroup > 0) {
      for (int i = 0; i < 32; i++) { // Semicolon fix
        if (buttonGroups[currentPage][i] == myGroup && i != noteNumber && i != PAGE_BUTTON) {
          usbMIDI.sendNoteOff(i, 0, midiChannel);
        }
      }
    }
    usbMIDI.sendNoteOn(noteNumber, 127, midiChannel);
  } 

  if (state == RELEASED && noteNumber != PAGE_BUTTON)  {
    int midiChannel = (noteNumber == 31 || noteNumber == 30) ? 1 : currentPage + 1;
    usbMIDI.sendNoteOff(noteNumber, 0, midiChannel);
  }
}

//---------------------------
//      UI Feedback
//---------------------------

void blinkPageLED() {
  for (int i =0; i <= currentPage; i++) {
    digitalWrite(ledPin, HIGH);
    delay (150);
    digitalWrite(ledPin, LOW);
    delay (150);
  }
}

//---------------------------
//      CLI Interface
//---------------------------

void processCLI() {
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();


  //View Groups CMD:
  if (cmd.equalsIgnoreCase("VG")) {
    Serial.println("====Current Group Assignments====");
    bool found = false;
    for (int p =0; p < NUM_PAGES; p++) {
      for (int i = 0; i < 32; i++) {
        if (buttonGroups[p][i] > 0) {
          Serial.print("Page: "); Serial.print(p + 1);
          Serial.print("  | Button: "); Serial.print(i);
          Serial.print("  | Group: "); Serial.println(buttonGroups[p][i]);
          found = true;
      
        }
      }
    }
    if (!found) Serial.println("No groups assigned.");
    Serial.println("---------------------------------");
  }


  // Format: "G<Page>:<Note>:<Group>" (e.g., "G1:5:2" = Page 1, Button 5, Group 2)
  //Handle group creation/assignment
  if (cmd.startsWith("G")) {
    int firstColon = cmd.indexOf(':');
    int secondColon = cmd.indexOf(':', firstColon + 1);

    if (firstColon > 0 && secondColon > 0) {
      int p = cmd.substring(1, firstColon).toInt() - 1;
      int note = cmd.substring(firstColon + 1, secondColon).toInt();
      int group = cmd.substring(secondColon + 1).toInt();

      if (p >=0 && p < NUM_PAGES && note >= 0 && note < 32) {
        buttonGroups[p][note] = group;
        EEPROM.put(0, buttonGroups);
        Serial.println("Group Saved.");
      } 
    }
  }
  //Handle Group Deletion
  else if (cmd.startsWith("D")) {
    int colon = cmd.indexOf(':');
    
    if (colon > 0) {
      int p = cmd.substring(1, colon).toInt() - 1;
      int note = cmd.substring(colon + 1).toInt();
      buttonGroups[p][note] = 0;
      EEPROM.put(0, buttonGroups);
      Serial.print("Group cleared for Page "); Serial.print(p+1);
      Serial.print(" Note "); Serial.println(note);
    }
  }
}