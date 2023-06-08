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

// chip and pin numbers to close switch number [index+1]
int Cnums[] = {5, 5, 4, 4, 3, 3};
int Pnums[] = {3, 1, 3, 1, 3, 1};

// to remember the last state, so that switches on the same chip can be controlled independently
byte commandBits[] = {0b00000100, 0b00000100, 0b00000100, 0b00000100, 0b00000100, 0b00000100};

int writeError;

String instring = "";
bool messageDone = true;

int switch_index;

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
  bitSet(commandBits[chipNum], pinNum+3);
  writeRegister(chipAddr[chipNum],IC_1,commandBits[chipNum]);
}

bool offOutput(int chipNum, int pinNum){
  bitClear(commandBits[chipNum], pinNum+3);
  writeRegister(chipAddr[chipNum],IC_1,commandBits[chipNum]);
}

void toggle(int switchN, bool open) {
  Serial.print("Toggling");
  Serial.print("switch" );
  Serial.print(switchN+1);
  if (open) {
    Serial.print(" open, chip ");
    offOutput(Cnums[switchN], Pnums[switchN]);
  } else {
    Serial.print(" closed, chip ");
    onOutput(Cnums[switchN], Pnums[switchN]);
  }
  Serial.print(Cnums[switchN]);
  Serial.print(" pin ");
  Serial.println(Pnums[switchN]);
  Serial.println("Done.");
}

void messageParse(String message){
  // first 3 chars are the command
  if (message.substring(0,3) == "all") { // for open all
  // open all ports of a switch in sequential order
  // syntax: "all" (no args)
    for (i = 0; i < 5; i++) {
      toggle(i, true);
    }

  } else if (message.substring(0,3) == "tog") { // toggle
  // turn switch port open or closed
  // syntax: "tog [integer switch #] ['opn' / 'cls']"
  // switch and port number are 1-indexed in the command
    if (isDigit(message.charAt(4))) {
      if (message.substring(4).toInt() > 0 && message.substring(4).toInt() < 5) {
        switch_index =  message.substring(4).toInt() - 1;
          if (message.substring(6) == "opn") {
            toggle(switch_index, true);
          } else if (message.substring(6) == "cls") {
            toggle(switch_index, false);
          } else {
            Serial.println("! Please specify opn or cls (for open or close)");
          }
      } else {
        Serial.println("! Switch specified not in 1..4");        
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

