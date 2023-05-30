#include <Wire.h>
#include <ctype.h>

#define ADDR_REG 0x00
#define IC_1 0x01
#define IC_2 0x02
#define STAT_1 0x03
#define STAT_2 0x04

#define ADDR0 0x60

int i,j;
int nSleep = A3;
int Vout_read_pin = A6;
int Vout_set_pin = 11;
int iSense = A7;
int nFaultPins[] = {2,3,4,5,6,7};
byte chipAddr[] = {0x61,0x62,0x63,0x64,0x65,0x66};

// there may be a better way than look-up table
// in all, index [switch][port]

int offCnums[3][6] = {{5, 5, 5, 4, 4, 4}, {3, 3, 3, 3, 2, 2}, {2, 1, 1, 1, 1, 0}};
int offPnums[3][6] = {{2, 1, 0, 3, 2, 1}, {3, 2, 1, 0, 3, 2}, {0, 3, 2, 1, 0, 3}};
int onCnums[3][6] = {{5, 5, 5, 5, 4, 4}, {4, 3, 3, 3, 3, 2}, {2, 2, 1, 1, 1, 1}};
int onPnums[3][6] = {{3, 2, 1, 0, 3, 2}, {0, 3, 2, 1, 0, 3}, {1, 0, 3, 2, 1, 0}};

int Cnums[3][6];
int Pnums[3][6];

float Vout_meas;
float current;
float V_operating = 4.995; // 4.7 if on USB only power
float Rshunt = 0.1; // Ohms
float Gain = 50; // From datasheet
float currentScale = V_operating/(Rshunt * Gain)/1024.0*1000; // mA
int zeroCurrentLevel = 8; // what analogRead reads with no current

float Vout_set = 3.2; // can't go lower than 1.1 V, chips need 2.6 V anyway
int Vout_set_int;

int writeError;

String instring = "dur"; // print the pulse durations for opening/closing the switch on boot
bool messageDone = true;
bool streamOn = false;

int onPulseDuration = 20; // ms. Amount of time the switching current is on to open the switch
int offPulseDuration = 70; // ms. Ditto to close the switch

int switch_index;
int port_index;

int tick_count;
int ticks_per_read = 500;

float getCurrent(){
  return (analogRead(iSense)-zeroCurrentLevel)*currentScale;
}

int getCurrentRaw(){
  return analogRead(iSense);
}

float getVoltage(){
  return analogRead(Vout_read_pin)/1024.0 * V_operating;
}

void setVoltage(float Vout_set){
  Vout_set_int = Vout_set / V_operating * 256;
  analogWrite(Vout_set_pin, Vout_set_int);
}

void writeRegister(byte addr, byte reg, byte val){
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  writeError = Wire.endTransmission();
}

byte readRegister(byte addr, byte reg){
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(int(addr),1);
  byte retval = Wire.read();
  if(Wire.available() > 0){
    Serial.println("Warning: I2C buffer not empty");
  }
  return retval;
}

void initializeChips(){
  writeRegister(ADDR0,IC_2,0b01000000);
  for( i = 0;i < 9;i++){
    pinMode(nFaultPins[i],OUTPUT);
    digitalWrite(nFaultPins[i],LOW);
  }
  for( i = 0;i < 9;i++){
    digitalWrite(nFaultPins[i],HIGH);
    writeRegister(ADDR0,IC_1,0b00000100); // set outputs to I2C controlled, and mode to full-bridge.
    writeRegister(ADDR0,ADDR_REG,chipAddr[i]);
    digitalWrite(nFaultPins[i],LOW);
  }
  for (i = 0;i < 9;i++){
    pinMode(nFaultPins[i],INPUT);
    writeRegister(chipAddr[i],IC_2,0b00000000);
  }
}

bool onOutput(int chipNum, int pinNum){
  byte switchFlip;
  switch(pinNum){
    case 0:
    switchFlip = 0b00100100;
    break;
    case 1:
    switchFlip = 0b01000100;
    break;
    case 2:
    switchFlip = 0b00010100;
    break;
    case 3:
    switchFlip = 0b00001100;
    break;
  }
  writeRegister(chipAddr[chipNum],IC_1,switchFlip);
}

bool offOutput(int chipNum){
    writeRegister(chipAddr[chipNum],IC_1,0b00000100);
}

void toggle(int switchN, int portN, int duration, bool open) {
  Serial.print("Toggling");
  Serial.print("switch" );
  Serial.print(switchN+1);
  Serial.print(" port ");
  Serial.print(portN+1);
  if (open) {
    Serial.print("open, chip ");
    for (int k = 0; k < 3; k++) {
      for (int m = 0; m < 6; m++) {
        Cnums[k][m] = onCnums[k][m];
        Pnums[k][m] = onPnums[k][m];
      }
    }
  } else {
    Serial.print("closed, chip ");
    for (int k = 0; k < 3; k++) {
      for (int m = 0; m < 6; m++) {
        Cnums[k][m] = offCnums[k][m];
        Pnums[k][m] = offPnums[k][m];
      }
    }
  }
  Serial.print(Cnums[switchN][portN]);
  Serial.print(" pin ");
  Serial.println(Pnums[switchN][portN]);
  onOutput(Cnums[switchN][portN], Pnums[switchN][portN]);
  delay(duration);
  offOutput(Cnums[switchN][portN]);
  Serial.println("Done.");
}

void messageParse(String message){
  // first 3 chars are the command
  if (message.substring(0,3) == "dur") { // short for duration
  // print the pulse durations
  // syntax: "dur"
  Serial.print("Closing pulse duration is ");
  Serial.print(offPulseDuration);
  Serial.println(" ms");
  Serial.print("Opening pulse duration is ");
  Serial.print(onPulseDuration);
  Serial.println(" ms");

  } else if (message.substring(0,3) == "opd") { // "open duration"
  // set the opening pulse duration
  // syntax: "opd [integer duration in ms]"
    if (isDigit(message.charAt(4))) {
      onPulseDuration = message.substring(4).toInt();
      Serial.print("Set open-toggling pulse duration to ");
      Serial.print(onPulseDuration);
      Serial.println(" ms");
    } else {
      Serial.println("! Duration specified is NaN");
    }

  } else if (message.substring(0,3) == "cld") { // "close duration"
  // set the closing pulse duration
  // syntax: "cld [integer duration in ms]"
    if (isDigit(message.charAt(4))) {
      offPulseDuration = message.substring(4).toInt();
      Serial.print("Set closed-toggling pulse duration to ");
      Serial.print(offPulseDuration);
      Serial.println(" ms");
    } else {
      Serial.println("! Duration specified is NaN");
    }

  } else if (message.substring(0,3) == "vlt") { // short for voltage
    // set the pulse duration
    // syntax: "dur [integer duration in ms]"
    // if duration not specified, it prints the current duration instead
      if (isDigit(message.charAt(4))) {
        Vout_set = message.substring(4).toInt();
        setVoltage(Vout_set);
        Serial.print("Set voltage out to ");
        Serial.print(Vout_set);
        Serial.println(" ms");
      } else {
        Serial.println("! Voltage specified is NaN");
      }

  } else if (message.substring(0,3) == "stm") { // for "stream"
    // toggles printing the voltage and current every cycle
    streamOn = !streamOn;

  } else if (message.substring(0,3) == "all") { // for open all
  // open all ports of a switch in sequential order
  // syntax: "all [switch #]"
  // switch # is 1-indexed in the command

  if (isDigit(message.charAt(4))) {
    if (message.substring(4).toInt() > 0 && message.substring(4).toInt() < 4) { 
      switch_index = message.substring(4).toInt() - 1;
      for (i = 0; i < 6; i++) {
        toggle(switch_index, i, onPulseDuration, true);
      }
    } else {
      Serial.println("! Switch specified not in 1..3");        
    }
  } else {
    Serial.println("! Switch number specified is NaN");
  }

  } else if (message.substring(0,3) == "tog") { // toggle
  // turn switch port open or closed
  // syntax: "tog [integer switch #] [integer port #] ['opn' / 'cls']"
  // switch and port number are 1-indexed in the command
    if (isDigit(message.charAt(4))) {
      if (message.substring(4).toInt() > 0 && message.substring(4).toInt() < 4) {
        switch_index =  message.substring(4).toInt() - 1;
        if (isDigit(message.charAt(6))) {
            if (message.substring(6).toInt() > 0 && message.substring(6).toInt() < 7) {
              port_index = message.substring(6).toInt() - 1;
              if (message.substring(8) == "opn") {
                toggle(switch_index, port_index, onPulseDuration, true);
              } else if (message.substring(8) == "cls") {
                toggle(switch_index, port_index, offPulseDuration, false);
              } else {
                Serial.println("! Please specify opn or cls (for open or close)");
              }
            } else {
              Serial.println("! Port specified not in 1..6");
            }
          } else {
          Serial.println("! Port specified is NaN");
        }
      } else {
        Serial.println("! Switch specified not in 1..3");        
      }
    } else {
      Serial.println("! Switch number specified is NaN");
    }
  
  } else {
    Serial.println("! Unknown command");
  }
}

int error;

void setup() {
  Wire.begin();
  Serial.begin(9600);

  Serial.println("Initializing...");

  instring.reserve(200);

  pinMode(nSleep,OUTPUT);
  digitalWrite(nSleep,HIGH);

  pinMode(iSense, INPUT);
  pinMode(Vout_read_pin, INPUT);
  pinMode(Vout_set_pin, OUTPUT);

  setVoltage(Vout_set);
  delay(1000);

  initializeChips();
  for( i = 0;i < 6;i++){
    Wire.beginTransmission(chipAddr[i]);
    error = Wire.endTransmission();
    Serial.print("  \n");
    Serial.println(chipAddr[i],HEX);
    
    Serial.print("error is ");
    Serial.print(error);
    if (error == 0)
    {
      Serial.print("   I2C device found at address");
     
    }
    else if (error == 4){
      Serial.print("unknown error at address"); 
    }
      
  }
  Serial.println();
}

void loop() {

  if (messageDone) {
    instring.trim();
    messageParse(instring);
    instring = "";
    messageDone = false;
  }

  if (streamOn) {
    tick_count++;
    if (tick_count % ticks_per_read == 0) {
      Vout_meas = getVoltage();
      Serial.print("V: ");
      Serial.print(Vout_meas);
      Serial.println(" V");
      current = getCurrent();
      Serial.print("I: ");
      Serial.print(current);
      Serial.println(" mA");
      tick_count = 0;
    }
  }

}

// called once between each loop
void serialEvent() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    instring += inChar;
    if (inChar == '\n') {
      messageDone = true;
    }
  }
}

