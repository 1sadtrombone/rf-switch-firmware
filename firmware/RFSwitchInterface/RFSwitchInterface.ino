#include <Wire.h>
#include <ctype.h>

#define ADDR_REG 0x00
#define I2C_1 0x01
#define I2C_2 0x02
#define STAT_1 0x03
#define STAT_2 0x04

#define ADDR0 0x60

int i,j;
int nSleep = A3;
int iSense = A7;
int nFaultPins[] = {2,3,4,5,6,7,8,9,10}; // try 2, .. 10
byte chipAddr[] = {0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69};

// chip and pin numbers to turn on/off port number [index+1]
int offCnums[] = {3, 3, 4, 4, 5, 5};
int offPnums[] = {1, 3, 1, 3, 1, 3};
int onCnums[] = {3, 3, 4, 4, 5, 5};
int onPnums[] = {0, 2, 0, 2, 0, 2};

float currentScale = 1.0;
int writeError;

String instring = "dur"; // print the pulse duration on boot
bool messageDone = true;

int pulseDuration = 50; // ms. Amount of time the switching current is on
int index;

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
  writeRegister(ADDR0,I2C_2,0b01000000);
  for( i = 0;i < 6;i++){
    pinMode(nFaultPins[i],OUTPUT);
    digitalWrite(nFaultPins[i],LOW);
  }
  for( i = 0;i < 6;i++){
    digitalWrite(nFaultPins[i],HIGH);
    writeRegister(ADDR0,I2C_1,0b00000100);
    writeRegister(ADDR0,ADDR_REG,chipAddr[i]);
    digitalWrite(nFaultPins[i],LOW);
  }
  for (i = 0;i < 6;i++){
    pinMode(nFaultPins[i],INPUT);
    writeRegister(chipAddr[i],I2C_2,0b00000000);
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
  writeRegister(chipAddr[chipNum],I2C_1,switchFlip);
}

bool offOutput(int chipNum){
    writeRegister(chipAddr[chipNum],I2C_1,0b00000100);
}

float getCurrent(){
  return analogRead(iSense)*currentScale;
}

void messageParse(String message){
  // first 3 chars are the command
  if (message.substring(0,3) == "dur") { // short for duration
  // set the pulse duration
  // syntax: "dur [integer duration in ms]"
  // if duration not specified, it prints the current duration instead
    if (isDigit(message.charAt(4))) {
      pulseDuration = message.substring(4).toInt();
      Serial.print("Set pulse duration to ");
      Serial.print(pulseDuration);
      Serial.println(" ms");
    } else if (message.substring(4).length() < 1) {
      Serial.print("Pulse duration is ");
      Serial.print(pulseDuration);
      Serial.println(" ms");
    } else {
      Serial.println("! Duration specified is NaN");
    }
  } else if (message.substring(0,3) == "tog") { // toggle
  // turn switch port open or closed
  // syntax: "tog [integer port N] ['opn' / 'cls']"
  if (isDigit(message.charAt(4))) {
      if (message.substring(4).toInt() > 0 && message.substring(4).toInt() < 7) {
        index = message.substring(4).toInt() - 1;
        if (message.substring(6) == "opn") {
          onOutput(onCnums[index], onPnums[index]);
          Serial.print("Toggling open, chip ");
          Serial.print(onCnums[index]);
          Serial.print(" pin ");
          Serial.println(onPnums[index]);
          delay(pulseDuration);
          offOutput(onCnums[index]);
          Serial.println("Done.");
        } else if (message.substring(6) == "cls") {
          onOutput(offCnums[index], offPnums[index]);
          Serial.print("Toggling closed, chip ");
          Serial.print(offCnums[index]);
          Serial.print(" pin ");
          Serial.println(offPnums[index]);
          delay(pulseDuration);
          offOutput(offCnums[index]);
          Serial.println("Done.");
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
    Serial.println("! Unknown command");
  }
}

int error;

int chipN = 3;
int onPinNum = 0;
int offPinNum = 1;

void setup() {
  Wire.begin();
  Serial.begin(9600);

  Serial.println("Initializing...");

  instring.reserve(200);

  pinMode(nSleep,OUTPUT);
  digitalWrite(nSleep,HIGH);

  pinMode(iSense, INPUT);

  initializeChips();
  Serial.println("got past init");
  for( i = 0;i < 9;i++){
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

