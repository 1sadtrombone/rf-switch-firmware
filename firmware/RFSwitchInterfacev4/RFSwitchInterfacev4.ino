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
int Vout_read_pin = A6;
int Vout_set_pin = 11;
int iSense = A7;
int nFaultPins[] = {2,3,4,5,6,7,8,9,10}; // try 2, .. 10
byte chipAddr[] = {0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69};

// chip and pin numbers to turn on/off [switch #][port #]
int Cnums[3][6] = {{0, 0, 1, 1, 2, 2}, {3, 3, 4, 4, 5, 5}, {6, 6, 7, 7, 8, 8}};
int opnPnums[3][6] = {{3, 1, 3, 1, 3, 1}, {3, 1, 3, 1, 3, 1}, {3, 1, 3, 1, 3, 1}};
int clsPnums[3][6] = {{2, 0, 2, 0, 2, 0}, {2, 0, 2, 0, 2, 0}, {2, 0, 2, 0, 2, 0}};

byte commandBits;

float Vout_meas;
float current;
float V_operating = 4.7;//4.995; // 4.7 if on USB only power
float Rshunt = 0.1; // Ohms
float Gain = 50; // From datasheet
float currentScale = V_operating/(Rshunt * Gain)/1024.0*1000; // mA
int zeroCurrentLevel = 11; // what analogRead reads with no current

float Vout_set = 3.8; // can't go lower than 1.1 V, chips need 2.7 V anyway
int Vout_set_int;

int writeError;
int error;

String instring = "dur"; // print the pulse durations on boot
bool messageDone = true;
bool streamOn = false;

int opnPulseDuration = 20; // ms. Amount of time the switching current is on to open the switch
int clsPulseDuration = 70; // ms. Ditto to close the switch

int switch_index;
int port_index;

int tick_count;
int ticks_per_read = 500;

unsigned long pulse_start; // for timing the pulses without delay()
int activeCnum = -1; // chip currently pulsing
int pulseDuration;

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
  for( i = 0;i < 9;i++){
    pinMode(nFaultPins[i],OUTPUT);
    digitalWrite(nFaultPins[i],LOW);
  }
  for( i = 0;i < 9;i++){
    digitalWrite(nFaultPins[i],HIGH);
    writeRegister(ADDR0,I2C_1,0b00000100);
    writeRegister(ADDR0,ADDR_REG,chipAddr[i]);
    digitalWrite(nFaultPins[i],LOW);
  }
  for (i = 0;i < 9;i++){
    pinMode(nFaultPins[i],INPUT);
    writeRegister(chipAddr[i],I2C_2,0b00000000);
  }
}

bool onOutput(int chipNum, int pinNum) {
  commandBits = 0b00000100;
  bitSet(commandBits, pinNum+3); // bits 3 to 7 set outputs 0 to 3.
  writeRegister(chipAddr[chipNum],I2C_1,commandBits);
}

bool offOutput(int chipNum){
  writeRegister(chipAddr[chipNum],I2C_1,0b00000100);
}

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

void messageParse(String message){
  // first 3 chars are the command
  if (activeCnum < 0) {
    if (message.substring(0,3) == "dur") { // short for duration
    // print the pulse durations
    // syntax: "dur"
    Serial.print("Closing pulse duration is ");
    Serial.print(clsPulseDuration);
    Serial.println(" ms");
    Serial.print("Opening pulse duration is ");
    Serial.print(opnPulseDuration);
    Serial.println(" ms");

    } else if (message.substring(0,3) == "opd") { // "open duration"
    // set the opening pulse duration
    // syntax: "opd [integer duration in ms]"
      if (isDigit(message.charAt(4))) {
        opnPulseDuration = message.substring(4).toInt();
        Serial.print("Set open-toggling pulse duration to ");
        Serial.print(opnPulseDuration);
        Serial.println(" ms");
      } else {
        Serial.println("! Duration specified is NaN");
      }

    } else if (message.substring(0,3) == "cld") { // "close duration"
    // set the closing pulse duration
    // syntax: "cld [integer duration in ms]"
      if (isDigit(message.charAt(4))) {
        clsPulseDuration = message.substring(4).toInt();
        Serial.print("Set closed-toggling pulse duration to ");
        Serial.print(clsPulseDuration);
        Serial.println(" ms");
      } else {
        Serial.println("! Duration specified is NaN");
      }

    } else if (message.substring(0,3) == "vlt") { // short for voltage
    // set the arduino output voltage
    // syntax: "vlt [float voltage in V]"
      if (isDigit(message.charAt(4))) {
        Vout_set = message.substring(4).toFloat();
        setVoltage(Vout_set);
        Serial.print("Set voltage out to ");
        Serial.print(Vout_set);
        Serial.println(" V");
      } else {
        Serial.println("! Voltage specified is NaN");
      }

    } else if (message.substring(0,3) == "stm") { // for "stream"
      // toggles printing the voltage and current every cycle
      streamOn = !streamOn;

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
                  onOutput(Cnums[switch_index][port_index], opnPnums[switch_index][port_index]);
                  Serial.print("Toggling open, chip ");
                  Serial.print(Cnums[switch_index][port_index]);
                  Serial.print(" pin ");
                  Serial.println(opnPnums[switch_index][port_index]);
                  activeCnum = Cnums[switch_index][port_index];
                  pulse_start = millis();
                  pulseDuration = opnPulseDuration;
                } else if (message.substring(8) == "cls") {
                  onOutput(Cnums[switch_index][port_index], clsPnums[switch_index][port_index]);
                  Serial.print("Toggling closed, chip ");
                  Serial.print(Cnums[switch_index][port_index]);
                  Serial.print(" pin ");
                  Serial.println(clsPnums[switch_index][port_index]);
                  activeCnum = Cnums[switch_index][port_index];
                  pulse_start = millis();
                  pulseDuration = clsPulseDuration;
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
  } else {
    Serial.println("! Control board busy pulsing");
  }
}

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
  for( i = 0;i < 9;i++){
    Wire.beginTransmission(chipAddr[i]);
    error = Wire.endTransmission();
    if (error == 0)
    {
      Serial.print("I2C device found at address ");
      Serial.println(chipAddr[i],HEX);
    }
    else {
      Serial.print("! Error ");
      Serial.print(error);
      Serial.print(" at address "); 
      Serial.println(chipAddr[i],HEX);
    }
  }
}

void loop() {

  if (messageDone) {
    instring.trim();
    messageParse(instring);
    instring = "";
    messageDone = false;
  }

  if (activeCnum >= 0) {
    if (millis() - pulse_start >= pulseDuration) {
      offOutput(activeCnum);
      activeCnum = -1;
      Serial.println("Done.");
    }
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

