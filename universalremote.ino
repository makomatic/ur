#include <Arduino.h> 
#include <avr/sleep.h>
#include <EEPROM.h> // 4KB EEPROM in mega2560
                    // Nano should have 1KB I think
                    // EEPROM has ~100000 write/erase circles
#include <EEPROMAnything.h> // additional helper
                            // to store whole structs with one call
#include <IRremote.h>
                    
int wakePin = 2; // pin used for waking up
int irReceivePin = 4;
//int irSendPin = 5; -> this is predefined by the irlib:
                        //TIMER_2 always uses Pin9 at mega2560
int statusLedPin = 13;
int buttonPin = 6;
// struct buttonmatrix{} -> TODO: Buy Button Matrix :)
int sleepStatus = 0;             // variable to store a request for sleep
int count = 0;                   // counter
int lastButtonState;

struct config_t
{
  int b1;
  // int b2, and so on
};
config_t configuration = {};

// Storage for the recorded ir code
  int codeType = -1; // The type of code
  unsigned long codeValue; // The code value if not raw
  unsigned int rawCodes[RAWBUF]; // The durations if raw
  int codeLen; // The length of the code
  int toggle = 0; // The RC5/6 toggle state

IRrecv irrecv(irReceivePin);
IRsend irsend;
decode_results results;

void wakeUpNow()        // here the interrupt is handled after wakeup
{
  // execute code here after wake-up before returning to the loop() function
  // timers and code using timers (serial.print and more...) will not work here.
  // we don't really need to execute any special functions here, since we
  // just want the thing to wake up
}
 
void setup()
{
  EEPROM_readAnything(0, configuration);
  Serial.begin(9600);
  Serial.println("Config loaded - Button 1: "+configuration.b1);
  delay(100);
  irrecv.enableIRIn(); // Start the receiver
  pinMode(wakePin, INPUT);
  pinMode(statusLedPin, OUTPUT);
  //pinMode(buttonmatrix, INPUT);
  attachInterrupt(0, wakeUpNow, LOW); // use interrupt 0 (pin 2) and run function
                                      // wakeUpNow when pin 2 gets LOW
}

// Stores the code for later playback
// Most of this code is just logging
void storeCode(decode_results *results) {
  codeType = results->decode_type;
  int count = results->rawlen;
  if (codeType == UNKNOWN) {
    Serial.println("Received unknown code, saving as raw");
    delay(100);
    codeLen = results->rawlen - 1;
    // To store raw codes:
    // Drop first value (gap)
    // Convert from ticks to microseconds
    // Tweak marks shorter, and spaces longer to cancel out IR receiver distortion
    for (int i = 1; i <= codeLen; i++) {
      if (i % 2) {
        // Mark
        rawCodes[i - 1] = results->rawbuf[i]*USECPERTICK - MARK_EXCESS;
        Serial.print(" m");
      } 
      else {
        // Space
        rawCodes[i - 1] = results->rawbuf[i]*USECPERTICK + MARK_EXCESS;
        Serial.print(" s");
      }
      Serial.print(rawCodes[i - 1], DEC);
    }
    Serial.println("");
  }
  else {
    if (codeType == NEC) {
      Serial.print("Received NEC: ");
      if (results->value == REPEAT) {
        // Don't record a NEC repeat value as that's useless.
        Serial.println("repeat; ignoring.");
        return;
      }
    } 
    else if (codeType == SONY) {
      Serial.print("Received SONY: ");
    } 
    else if (codeType == RC5) {
      Serial.print("Received RC5: ");
    } 
    else if (codeType == RC6) {
      Serial.print("Received RC6: ");
    } 
    else {
      Serial.print("Unexpected codeType ");
      Serial.print(codeType, DEC);
      Serial.println("");
    }
    Serial.println(results->value, HEX);
    codeValue = results->value;
    codeLen = results->bits;
  }
}

void sendCode(int repeat) {
  if (codeType == NEC) {
    if (repeat) {
      irsend.sendNEC(REPEAT, codeLen);
      Serial.println("Sent NEC repeat");
    } 
    else {
      irsend.sendNEC(codeValue, codeLen);
      Serial.print("Sent NEC ");
      Serial.println(codeValue, HEX);
    }
  } 
  else if (codeType == SONY) {
    irsend.sendSony(codeValue, codeLen);
    Serial.print("Sent Sony ");
    Serial.println(codeValue, HEX);
  } 
  else if (codeType == RC5 || codeType == RC6) {
    if (!repeat) {
      // Flip the toggle bit for a new button press
      toggle = 1 - toggle;
    }
    // Put the toggle bit into the code to send
    codeValue = codeValue & ~(1 << (codeLen - 1));
    codeValue = codeValue | (toggle << (codeLen - 1));
    if (codeType == RC5) {
      Serial.print("Sent RC5 ");
      Serial.println(codeValue, HEX);
      irsend.sendRC5(codeValue, codeLen);
    } 
    else {
      irsend.sendRC6(codeValue, codeLen);
      Serial.print("Sent RC6 ");
      Serial.println(codeValue, HEX);
    }
  } 
  else if (codeType == UNKNOWN /* i.e. raw */) {
    // Assume 38 KHz
    irsend.sendRaw(rawCodes, codeLen, 38);
    Serial.println("Sent raw");
  }
}

void sleepNow()
{
  /*    Possible Sleep States:
  *     SLEEP_MODE_IDLE         -the least power savings
  *     SLEEP_MODE_ADC
  *     SLEEP_MODE_PWR_SAVE
  *     SLEEP_MODE_STANDBY
  *     SLEEP_MODE_PWR_DOWN     -the most power savings
  */  
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  attachInterrupt(0,wakeUpNow, LOW);
  sleep_mode();            // here the device is actually put to sleep!!
                           // THE PROGRAM CONTINUES FROM HERE AFTER WAKING UP
 
  sleep_disable();         // first thing after waking from sleep:
                           // disable sleep...
  detachInterrupt(0);      // disables interrupt 0 on pin 2 so the
                           // wakeUpNow code will not be executed
                           // during normal running time.
  Serial.println("Woke up");
  delay(100);
}

void saveConfig()
{
  EEPROM_writeAnything(0, configuration);
  Serial.println("Configuration saved");
  delay(100);
}

void loadConfig()
{
  Serial.println("Configuration loaded");
  delay(100);
  EEPROM_readAnything(0, configuration);
  Serial.println(configuration.b1);
  delay(100);
}

void getIR()
{
// we have to wait for input here
// messy serial debug shit... we have to avoid too much tests here
// better buy a button matrix soon!
  if (irrecv.decode(&results)) {
    digitalWrite(statusLedPin, HIGH);
    storeCode(&results);
    irrecv.resume(); // resume receiver
    digitalWrite(statusLedPin, LOW);
  }
}

void sendIR()
{
  digitalWrite(statusLedPin, HIGH);
  //sendCode(lastButtonState == buttonState);
  digitalWrite(statusLedPin, LOW);
  delay(50); // Wait a bit between retransmissions

  //int buttonState = digitalRead(BUTTON_PIN);
  //if (lastButtonState == HIGH && buttonState == LOW) {
  //  Serial.println("Released");
  //  irrecv.enableIRIn(); // Re-enable receiver
  //}
}
  
void configButton()
{
  Serial.println("Press source button");
  delay(100);
  getIR();
  Serial.println("Press destination button");
  delay(100);
  configuration.b1 = Serial.read();
}

void loop()
{
  // If button pressed, send the code.
  int buttonState = digitalRead(buttonPin);
  if (lastButtonState == HIGH && buttonState == LOW) {
    Serial.println("Released");
    irrecv.enableIRIn(); // Re-enable receiver
  }

  if (buttonState) {
    Serial.println("Pressed, sending");
    digitalWrite(statusLedPin, HIGH);
    sendCode(lastButtonState == buttonState);
    digitalWrite(statusLedPin, LOW);
    delay(50); // Wait a bit between retransmissions
  } 
  else if (irrecv.decode(&results)) {
    digitalWrite(statusLedPin, HIGH);
    storeCode(&results);
    irrecv.resume(); // resume receiver
    digitalWrite(statusLedPin, LOW);
  }
  lastButtonState = buttonState;
  
  count++;
  // compute the serial input
  if (Serial.available()) {
    int val = Serial.read();
    switch (val) {
      case 'c':
        configButton();
        break;
      case 's':
        saveConfig();
        break;
      case 'l':
        loadConfig();
        break;
      case 'S':
        Serial.println("Going to sleep");
        delay(100);
        count = 0;
        sleepNow();
    }
  }
 
  if (count >= 600) {
      Serial.println("Timer: Entering Sleep mode");
      delay(100);
      count = 0;
      sleepNow();
  }
  delay(100);
}
