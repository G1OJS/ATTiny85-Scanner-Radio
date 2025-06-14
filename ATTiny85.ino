#include <G1OJS_Si5351.h>

#include "Wire.h"
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>
//#include "Arduino.h"

#define AGC_OUT PB1     // ATtiny85 physical pin 6
#define SQUELCH_OUT PB3 // ATtiny85 physical pin 2
#define AUDIO_IN A2    // ATtiny85 physical pin 3
// SDA is PB0 ATtiny85 physical pin 5
// SCL is PB2 ATtiny85 physical pin 7

// =================
// proxy for ms() as timer0 will be running at 4kHz not 500Hz
//==================
unsigned long ms(){return millis()/8;} 

G1OJS_Si5351 DDS;

// =================
// AUDIO MONITOR
//==================
struct {
  int currentAmplitude=0;

  init(){
    analogReference(DEFAULT); 
    pinMode(AUDIO_IN, INPUT);
    currentAmplitude=0;
  }

  void updateCurrentAmplitude(){
    int currSigLevel=analogRead(AUDIO_IN); 
    currentAmplitude=abs(currSigLevel-lastSigLevel);
    lastSigLevel=currSigLevel;
  }
  
  private:
    int lastSigLevel=0;

} preSquelchAudioMonitor;

// =================
// OLED
//==================
struct {
  #define OLED_I2C_ADDRESS 0x3C
  SSD1306AsciiWire OLED;

  void init(){

    Wire.setClock(400000);
    Wire.begin();
    do{
      Wire.beginTransmission(0x3C);  // Known I2C address (e.g., OLED)
      delay(10);
    } while (Wire.endTransmission() != 0);
    OLED.begin(&Adafruit128x64, OLED_I2C_ADDRESS);
    OLED.clear();

  	OLED.setFont(Arial14);
  	OLED.set2X();
  	OLED.clear();
    OLED.println("G1OJS");
    OLED.println("ATTiny85");
    delay(500);
  	OLED.clear();
    OLED.println("Airband");
    OLED.println("Scanner");
    delay(500);
  	OLED.clear();
  }

  void showSquelchState(char* symbol){
    OLED.setCursor(100,0);
    OLED.print(symbol);
  }

  void showChannel(byte channel) 
    {
    OLED.setCursor(0, 0);
    ultoa((unsigned long)channel, buf, 10);
    OLED.print(buf);
  }

  void showFreq(unsigned long freq_Hz) 
    {
    OLED.setCursor(0, 4);
    ultoa(freq_Hz/1000, buf, 10);
    OLED.print(buf);
  }

  void clearFreq(){
    
  }

  private:
    char buf[6];

} DISP;

// =================
// SQUELCH
//==================
struct {
  enum statusValues {OPEN=0, CLOSED=1};
  statusValues status = CLOSED;

  void init() {
    pinMode(SQUELCH_OUT, OUTPUT);
    close();
  }

  void open() {
    status = OPEN;
    digitalWrite(SQUELCH_OUT, LOW);
    DISP.showSquelchState("O");
  }

  void close() {
    status = CLOSED;
    digitalWrite(SQUELCH_OUT, HIGH);
    DISP.showSquelchState("C");
  }

  void implement() {
    if (preSquelchAudioMonitor.currentAmplitude > threshold) {
      if (status == CLOSED) {
        open();
      }
      lastAboveThreshold_ms = ms();
    } else {
      if ((ms() - lastAboveThreshold_ms) > tail_ms) {close();}
    }
  }

  private:
    const int tail_ms   = 1000;
    const int threshold = 40;
    unsigned long lastAboveThreshold_ms;

} squelch;

// =================
// SCANNER
//==================
struct {
  #define channelStep 25000
  #define nChannels 5
  unsigned long channels_Hz[nChannels]= {128602650, 127825000, 132846700, 135050000, 129425000};
  boolean lockout[nChannels] = {1,0,0,0,0};
  int channel;
  enum scannermodes{SCAN, SEARCH};
  scannermodes mode;

  void init(){
    channel = 0;
    mode = SCAN;
  }

  void setTestFreq(){
    setRxFreq(128602650UL);
    DISP.showChannel(0);
    DISP.showFreq(128602650UL); 
  }

  void findNextBusyMemory(){
    if (squelch.status == squelch.OPEN) return;
    do{channel = (channel + 1) % nChannels;} while (lockout[channel]);

    DISP.showChannel(channel);
    if(channels_Hz[channel] != lastFreq) {
      setRxFreq(channels_Hz[channel]);
      DISP.showFreq(channels_Hz[channel]); 
      lastFreq = channels_Hz[channel];
    }
  }

  void findNextBusyFrequency(){
    searchFreq += channelStep;
    if (searchFreq > 136000000ULL) {
      init();
    }

    setRxFreq(searchFreq);
    DISP.showFreq(searchFreq);   // probably don't do this here (speed?)
    // may need a mechanism to dwell or simply revisit? 
    if (squelch.status == squelch.OPEN) {
      // do something on display to show searching & search speed, and/or channel found
      DISP.showFreq(searchFreq); 
      delay(100);
      // check frequency and add etc
    }
  }

  void startSearch(){
    searchFreq = 118000000ULL;
    mode = SEARCH;
  }

  void setRxFreq(unsigned long freq_Hz){
    #define IF_kHz 10707
    DDS.set_freq((uint64_t)freq_Hz + (uint64_t)IF_kHz*1000); // Start CLK0 (VFO)
  }

  void scan(){
    if (mode == SCAN) {findNextBusyMemory();}
    if (mode == SEARCH) {findNextBusyFrequency();}
  }

  private:
    unsigned long lastFreq;
    unsigned long searchFreq;


} scanner;

// =================
// AGC LOOP
//==================
struct {

  void init() {
    // Configure PB1 (OC1B) as output
    DDRB |= _BV(PB1); // PB1 = physical pin 6

    // Setup Timer1 for 8-bit Fast PWM on OC1B (PB1)
    // Set both PB0 (pin5) and PB1 (pin6) to 4kHz non-inverted PWM
    // see https://www.gammon.com.au/timers
    TCCR0A = bit (WGM00) | bit (WGM01) | bit (COM0A1);  // Timer 0, A side: fast PWM, clear OC0A on compare
    TCCR0B = bit (CS01);                                // Timer 0, A side: fast PWM, top at 0xFF, prescale x8 (4kHz)
    TCCR0A |= bit (COM0B1);                             // Timer 0, B side: clear OC0B on compare
  }

  void implement(){
    int currAmp=preSquelchAudioMonitor.currentAmplitude;
    if (currAmp > thresholdUpper) {level += 2*(level < maxOutputLevel);}
    if (currAmp < thresholdLower) {level -= 1*(level > minOutputLevel);}
    OCR0B = level;
  }

  private:
    const byte thresholdUpper = 160;
    const byte thresholdLower = 20;
    const byte maxOutputLevel = 250;
    const byte minOutputLevel = 110;
    byte level = minOutputLevel;

} agc;



// =================
// NON-BLOCKING TASK RUNNER
//==================
class regularTask {
public:
  unsigned long last_ms;
  byte interval_ms;
  void (*callback)();

  regularTask(byte interval, void (*cb)()) {
    interval_ms = interval;
    last_ms = ms();
    callback = cb;
  }

  void runIfDue() {
    unsigned long now = ms();
    if (now - last_ms >= interval_ms) {
      last_ms = now;
      if (callback) callback();
    }
  }
};

// Create tasks to 'runIfDue()' in loop()
regularTask squelchTask(50, []() { squelch.implement(); });
regularTask AGCTask(100,  []() { agc.implement();});
regularTask ScannerTask(50,  []() { scanner.scan();});
regularTask audioMonitorTask(50,  []() {   preSquelchAudioMonitor.updateCurrentAmplitude();});

void setup()
{
  delay(500);  // Let power settle
  DISP.init();
  preSquelchAudioMonitor.init();
  squelch.init();
  agc.init();
  scanner.init();

 // scanner.startSearch();

}


void loop() {
  
  squelchTask.runIfDue();
  AGCTask.runIfDue();
  ScannerTask.runIfDue();
  audioMonitorTask.runIfDue();
  
}



