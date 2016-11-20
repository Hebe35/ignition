#include <max6675.h>
#include <LiquidCrystal.h>
#include <avr/wdt.h>
#include <Timer.h>

#define CHOKE_RELAY_PIN           A14      // Pin for the choke relay 
#define INGNITION_STOP_RELAY_PIN  A15      // Pin for the ingnition stop relay 

const int ignitionPin           = 2;       // The pin for the Arduino interrupt
const int ignitionInterrupt     = 0;       // The interrupt number corresponding to the interrupt pin
const unsigned int pulsesPerRev = 1;       // The number of ignition pulses per engine revolution

// Muuttujat

const long minRpm               = 5000;    // The LED bar starts at 5000 RPM
const long maxRpm               = 13000;   // The last LED's light up at 14000 RPM
const long maxRpmLimitter       = 14000;   // The motor RPM limiter up at 14000 RPM
const long okRpm                = 13000;   // The ok 12000 RPM
const long blinkRpm             = 12000;   // The blinking starts at 11500 RPM -> shift light
const long egtHot               = 590;     // The EGT hot         600°C
const long choke                = 700;     // The Choke           700°C
const long ingnitionStop        = 630;     // The ingnition stop  620°C 
const long egtHotLed            = 600;     // The EGT hot red led 600°C
const long egtDiff_             = 0.91667; // The EGT diff - and red led 0.91667 ~50°C
const long egtDiff              = 1.09;    // The EGT diff + and red led 1.09    ~50°C

int RPM = 1; // 0 ei näytetä / 1 näytetään




// Interrupt variables which are accessed outside of the interrupt handler must be declared as 'volatile'
volatile unsigned long lastPulseTime = 0;
volatile unsigned long rpm = 0;

  
// The timers
Timer timer;

long displayRpm; // The displayed RPM figure
int ledLimit;    // The amount of LED's turned on
bool blinking;   // Whether the LED's should be blinking or not (blinkRpm exceeded)

const int firstLedPin = 22;
// Pins 22 to 51 are for the LED bar:
// 22 - 27 & 51 - 46: green LED's
// 28 - 30 & 45 - 43: yellow LED's
// 31 - 33 & 42 - 40: amber LED's
// 34 - 36 & 39 - 37: red LED's
const int lastLedPin = 51;
const int warningLed = 52;    // The 'WARNING Led' LED

// Exhaust thermocouples
int thermo1DO = 16;  // EGT 1 pin 16
int thermo1CS = 15;  // EGT 1 pin 15
int thermo1CLK = 14; // EGT 1 pin 14
int thermo2DO = 17;  // EGT 2 pin 17
int thermo2CS = 18;  // EGT 2 pin 18
int thermo2CLK = 19; // EGT 2 pin 19

MAX6675 thermocouple1(thermo1CLK, thermo1CS, thermo1DO);
MAX6675 thermocouple2(thermo2CLK, thermo2CS, thermo2DO);

float thermocouple1_temp;
float thermocouple2_temp;

// Display
// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(8, 9, 10, 11, 12, 13);

  // make a cute degree symbol
uint8_t degree[8]  = {140,146,146,140,128,128,128,128};

// these are the pins connected to the RGB LEDs
// Display
int LCDBacklights[] = {7,6,5}; 


void setup() {  
  Serial.begin(9600);  
  
   // Ingnition relay
  pinMode(INGNITION_STOP_RELAY_PIN, OUTPUT);

  // Default mode for the ingnition is OFF
  digitalWrite(INGNITION_STOP_RELAY_PIN, HIGH);  // ingnition stop to OFF state
  
   // Choke relay
  pinMode(CHOKE_RELAY_PIN, OUTPUT);

  // Default mode for the choke is OFF
  digitalWrite(CHOKE_RELAY_PIN, HIGH);  // choke to OFF state
  
  
  // LCD initialization
  lcd.begin(16, 2);
  lcd.createChar(0, degree);
  
   for(int i = 0 ; i < 3; i++ ){
    pinMode(LCDBacklights[i], OUTPUT);
    digitalWrite(LCDBacklights[i], LOW);
    lcd.setCursor(0, 0);
    lcd.clear();
    if (i==0)
    lcd.print("TEST RED");
    if (i==1)
    lcd.print("TEST GREEN");
    if (i==2)
    lcd.print("TEST BLUE");
    delay(100); // 600
    lcd.clear();
    digitalWrite(LCDBacklights[i], HIGH);
    delay(100); // 400
  }

  // Attach the interrupt handler to the interrupt
  pinMode(ignitionPin, INPUT);
  attachInterrupt(ignitionInterrupt, &ignitionIsr, RISING);
  
  // Pull-up for interrupt pins
  digitalWrite(2, HIGH);
  
  // Pull-up for not used pins
  pinMode(1, OUTPUT);
  digitalWrite(1, LOW);
  pinMode(3, OUTPUT);
  digitalWrite(3, LOW) ;
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
  
  // Pull-doun for not used pins
  for (byte i = A1; i <= A13; i++) {
    pinMode(i, OUTPUT);
    digitalWrite(i,  LOW);
  }

  // Light up all LED's, including the 'WARNING ON' LED
  for (byte i = firstLedPin; i <= warningLed; i++) {
    pinMode(i, OUTPUT);
    digitalWrite(i,  HIGH);
  }

  // All LED's light up for two seconds at power on. The 'POWER ON' LED is never turned off  
  delay(100); // 2000
  
   digitalWrite(warningLed, LOW);

// The timed calls
  timer.every(2000, feedWatchdog);                // every 2 seconds
  timer.every(890, updateDisplay);                // every 890ms
  timer.every(540, readEGTSensors);               // every 540ms
  timer.every(100, ignitionPulseReadRpmLEDbar);   // every 100ms  The rpm LED bar is updated every 100ms, i.e. 10 times per second
  timer.every(1550, backgroundcolorRGB);          // every 1550ms
  
   // Enable watchdog
  wdt_enable(WDTO_8S);
  
  }
  
void loop(){

  timer.update();
 
 }
 
 // For each of the LED's
  void ignitionPulseReadRpmLEDbar(){
  // Read the RPM figure
  noInterrupts();
  displayRpm = int(rpm);
  // If the last pulse was more than a second ago, the RPM figure is 0
  if ((micros() - lastPulseTime) > 1000000) {
    displayRpm = 0;
  }
  interrupts();

  // Calculate how many LED's should light up
  if (displayRpm < minRpm ) {
    ledLimit = 0;
  } else {
    ledLimit = ((lastLedPin - firstLedPin + 1)/2 * (displayRpm - (minRpm-(maxRpm - minRpm)/15))) / (maxRpm - (minRpm-(maxRpm - minRpm)/15));  
  //  ledLimit = ((lastLedPin - firstLedPin + 1)/2 * (displayRpm - minRpm)) / (maxRpm - minRpm);
  }

  // Is the RPM figure over the shift limit?
  if (displayRpm > blinkRpm) {
    blinking = !blinking;
  } else {
    blinking = false;
  }

  // Debug output
  Serial.print("RPM: ");
  Serial.println(displayRpm);
  Serial.print("Number of LED's: ");
  Serial.println(ledLimit);
     
  for (int i = 0; i < (lastLedPin - firstLedPin + 1)/2; i++) {
    if (i < ledLimit) {
      // The LED should be on or blinking
      digitalWrite(firstLedPin + i, blinking?LOW:HIGH);     
      digitalWrite(lastLedPin - i, blinking?LOW:HIGH); 
    } else {
      // The LED should be off
      digitalWrite(firstLedPin + i, LOW);     
      digitalWrite(lastLedPin - i, LOW);     
    }
  }}
// Update the EGT temp
void readEGTSensors(){

  thermocouple1_temp = (thermocouple1.readCelsius()-2); //read_temp()-lämpötilakorjaus-2;
  thermocouple2_temp = (thermocouple2.readCelsius()-1); //read_temp()-lämpötilakorjaus-1;

  Serial.print("thermocouple 1: ");
  Serial.println(int(thermocouple1_temp));
  Serial.print("thermocouple 2: ");
  Serial.println(int(thermocouple2_temp));
  
  }
  // LCD EGT print;
  // Update the LCD display
 void updateDisplay()
 {
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("EGT "); // 1
  lcd.setCursor(0, 1);
  lcd.print("EGT "); // 2
  lcd.setCursor(4,0);
  lcd.print(int(thermocouple1_temp));
  //lcd.write((byte)0);
  lcd.print("\xDF""C ");
  lcd.setCursor(4,1);
  lcd.print(int(thermocouple2_temp));
  //lcd.write((byte)0);
  lcd.print("\xDF""C ");
  lcd.setCursor(10,0);
  
  if ((int(thermocouple1_temp)) > egtHot ) { // 600 °C
  lcd.print("HOT!!! ");     
//} else  if ((int(thermocouple1_temp)) > 550 °C ) { // 550 °C
//  lcd.print("OK ");     
} else if ( egtDiff < (int(thermocouple1_temp)/(int(thermocouple2_temp))) > egtDiff_ ) { //  lämpötila ero 50 °C = 600 °C
  lcd.print("DIF 50! ");
  // Jos tarvetta kierroksille RPM ==1 if (RPM == 1 ) 
} else if (RPM == 1 ) { // 1 
  lcd.setCursor(10,0);
  lcd.print("RPM");
  }
  lcd.setCursor(14,0);
  if (RPM == 1 && ((displayRpm)>maxRpm  && (int(thermocouple1_temp)) <= egtHot && ( egtDiff < (int(thermocouple1_temp)/(int(thermocouple2_temp))) < egtDiff_ ))) { //14000 rpm
  lcd.print("HI");
} else  if (RPM == 1 && ((displayRpm)>minRpm && (displayRpm)<okRpm  && (int(thermocouple1_temp)) <= egtHot && ( egtDiff < (int(thermocouple1_temp)/(int(thermocouple2_temp))) < egtDiff_ ))) { //14000 rpm  
  lcd.print("OK");
} else  if (RPM == 1 && ((displayRpm)<minRpm && (int(thermocouple1_temp)) <= egtHot && ( egtDiff < (int(thermocouple1_temp)/(int(thermocouple2_temp))) < egtDiff_ ))) { //14000 rpm
  lcd.print("LO");
}
  lcd.setCursor(10,1);
  if ((int(thermocouple2_temp)) > egtHot ) { // 580 °C
  lcd.print("HOT!!! ");  
//} else  if ((int(thermocouple2_temp)) > 550 °C ) { // 550 °C
//  lcd.print("OK ");     
} else if ( egtDiff < (int(thermocouple2_temp)/(int(thermocouple1_temp))) > egtDiff_ ) { //  lämpötila ero 50 °C = 600 °C
  lcd.print("DIF 50! ");
  // Jos tarvetta kierroksille RPM ==1 if (RPM == 1 ) 
} else if (RPM == 1 ) { // 1 
  lcd.setCursor(10,1);
  lcd.print(displayRpm);
  }
  lcd.setCursor(14,0);

    
  //warningLed
  
  if ((int(thermocouple1_temp) > egtHotLed ) || (int(thermocouple2_temp) > egtHotLed ))  {  // 600 °C
  digitalWrite(warningLed, HIGH);
} else if ( egtDiff < (int(thermocouple1_temp)/(int(thermocouple2_temp))) > egtDiff_ ) { //  lämpötila ero 50 C = 600 °C
  digitalWrite(warningLed, HIGH);
} else if ( egtDiff < (int(thermocouple2_temp)/(int(thermocouple1_temp))) > egtDiff_ ) { //  lämpötila ero 50 C = 600 °C
  digitalWrite(warningLed, HIGH);  
} else {
  digitalWrite(warningLed, LOW); 
}

//Ingnition stop
  
  if ((int(thermocouple1_temp) > ingnitionStop ) || (int(thermocouple2_temp) > ingnitionStop ) || (int(displayRpm) > maxRpmLimitter )) {  // 620 °C RPM 14000
  digitalWrite(INGNITION_STOP_RELAY_PIN, LOW);
} else {
  digitalWrite(INGNITION_STOP_RELAY_PIN, HIGH); 
}

//Choke
  
  if ((int(thermocouple1_temp) > choke ) || (int(thermocouple2_temp) > choke
  ))  {  // 700 C
  digitalWrite(CHOKE_RELAY_PIN , LOW);
} else {
  digitalWrite(CHOKE_RELAY_PIN , HIGH); 
}
}
   
  
// background color RGB
void backgroundcolorRGB(){
   
   if ((int(thermocouple1_temp) > 600 ) || (int(thermocouple2_temp) > 600 ))   {    // 600
   LCD_backlight(255,255,255);  // Hex FFFFFF white
}  else if ((int(thermocouple1_temp) > 550 ) || (int(thermocouple2_temp) > 550 )) { // 550
   LCD_backlight(255,0,0);      // Hex FF0000 red
}  else if ((int(thermocouple1_temp) > 500 ) || (int(thermocouple2_temp) > 500 )) { // 500
   LCD_backlight(224,10,10);  // Hex FF0A0A
}  else if ((int(thermocouple1_temp) > 450 ) || (int(thermocouple2_temp) > 450 )) { // 450
   LCD_backlight(255,25,25);  // Hex FF1919
}  else if ((int(thermocouple1_temp) > 400 ) || (int(thermocouple2_temp) > 400 )) { // 400
   LCD_backlight(255,51,51);    // Hex FF3333
}  else if ((int(thermocouple1_temp) > 300 ) || (int(thermocouple2_temp) > 300 )) { // 300
   LCD_backlight(255,255,0);    // Hex FFFF00 Yellow
}  else if ((int(thermocouple1_temp) > 200 ) || (int(thermocouple2_temp) > 200 )) { // 200
   LCD_backlight(51,51,255);    // Hex FFFF33
}  else if ((int(thermocouple1_temp) > 100 ) || (int(thermocouple2_temp) > 100 )) { // 100
   LCD_backlight(0,0,204);      // Hex 0000FF 
}  else if ((int(thermocouple1_temp) > -40 ) || (int(thermocouple2_temp) > -40 )) { // -40
   LCD_backlight(0,0,255);      // Hex 0000CC blue
 }  
}

// The ignitionIsr is called by the interrupt every time a spark is detected
void ignitionIsr()
{
  unsigned long now = micros();
  unsigned long interval = now - lastPulseTime;
  if (interval > 4000) // To ignore false ignition pulses. If the engine speed is below 15000 rpm, the ignition pulses are at least 4000 us apart
  {
     rpm = 60000000UL/(interval * pulsesPerRev);
     lastPulseTime = now;
  }  
}

void LCD_backlight(byte r, byte g, byte b){
  analogWrite(LCDBacklights[0],255- r); // RGB red
  analogWrite(LCDBacklights[1],255- g); // RGB green
  analogWrite(LCDBacklights[2],255- b); // RGB blue
  
  Serial.print("RGB red : ");
  Serial.println(r);
  Serial.print("RGB green : ");
  Serial.println(g);
  Serial.print("RGB blue :  ");
  Serial.println(b);
  
  }
  //
// The most important thing of all, feed the watchdog
//
void feedWatchdog()
{
  wdt_reset();
}
