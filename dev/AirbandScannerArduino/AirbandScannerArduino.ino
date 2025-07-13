#include <G1OJS_Tiny_Si5351_CLK0.h>
#include <G1OJS_Scanner.h>

#include "Arduino.h"

#include <Wire.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>


#define LCD_I2C_ADDRESS 0x3C
#define CORRECTION_FACTOR 159000

#define AGC_OUT 10
#define SQUELCH_OUT 8
#define AUDIO_IN A0
// SDA is A4
// SCL is A5

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
    last_ms = 0;
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

void OutputAGC() {
    AGC.updateAGCLevel();
 //   OCR0B = AGC.AGCLevel;
    analogWrite(AGC_OUT,AGC.AGCLevel);
}

void tickSerial(){
  Serial.print((int)(millis()/1000));
  Serial.print(" ");
}

// Create tasks to 'runIfDue()' in loop()
regularTask squelchTask(50, []() { Squelch.implement(); });
regularTask AGCTask(100,  []() { OutputAGC();});
regularTask ScannerTask(50,  []() { Scanner.scan();});
regularTask audioMonitorTask(50,  []() {  preSquelchAudioMonitor.updateCurrentAmplitude();});


void setup()
{
  Serial.begin(9600);
  Serial.println("Starting setup");
  
  TCCR1B = TCCR1B & B11111000 | B00000001; // set a high (~60kHz) PWM frequency to make filtering to DC easy
  pinMode(AGC_OUT ,OUTPUT);

  
  i2c.init();
  ScannerDisplay.init();
  preSquelchAudioMonitor.init(AUDIO_IN);
  AGC.init();
  Squelch.init(SQUELCH_OUT);
  Scanner.init();
  Scanner.doChannelSurvey();

}

void loop() {
//  squelchTask.runIfDue();
//  AGCTask.runIfDue();
//  ScannerTask.runIfDue();
//  audioMonitorTask.runIfDue();

}




