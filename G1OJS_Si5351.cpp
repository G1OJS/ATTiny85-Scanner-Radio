//=====================
//DDS 
//=====================
#include <stdint.h>
#include "Arduino.h"
#include "Wire.h"

#include "G1OJS_Si5351.h"


  #define XTAL 25000000                           // Crystal freq in Hz   
  #define CorrFact 0.999658117

  void i2cWrite(uint8_t reg, uint8_t val) {   // write reg via i2c
    Wire.beginTransmission(0x60);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
  }


  void G1OJS_Si5351::set_freq(uint32_t fout) { // set frequency fout Hz (CLK0 only)

    // initialise         
    delay(1);    
    i2cWrite(3, 0xFF);                                    // Disable all CLK output drivers
    for (int i = 16; i < 24; i++)
        i2cWrite (i, 0x80);                               // CLKi Control, power down CLKi
    i2cWrite(15, 0x00);           // PLL Input Source, select the XTAL input as the reference clock for PLLA and PLLB
    i2cWrite(24, 0x00);           // CLK3–0 Disable State, unused are low and never disable CLK0
    i2cWrite(16, 0x4F);           // Power up CLK0, PLLA, MS0 operates in integer mode, Output Clock 0 is not inverted, Select MultiSynth 0 as the source for CLK0 and 8 mA
 
    // Reference load configuration
    i2cWrite(183, 0x2);          // Set reference load C: 6 pF = 0x12, 8 pF = 0x92, 10 pF = 0xD2
 
    // Turn CLK0 output on
    i2cWrite(3, 0xFE);            // Output Enable Control. Active low
    //i2cWrite(149, 0);                                     // SpreadSpectrum off
                          
    // macros to split int32 into Bytes
    #define BB0(x) ((uint8_t)x)            
    #define BB1(x) ((uint8_t)(x>>8))
    #define BB2(x) ((uint8_t)(x>>16))
    //   see AN619 at https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN619.pdf:
    //   fout = fvco / (Output_Multisynth x RDIV)
    //   fvco = fin x Feedback_Multisynth
    //   Feedback_Multisynth (FMS) = FMSa + FMSb / FMSc
   
    uint32_t FMSa = 600000000UL / fout;                  // Use lowest VCO frequency but handle d minimum
    if (FMSa < 6)
        FMSa = 6;
    else if (FMSa % 2)                          // Make d even to reduce phase noise/jitter, see datasheet 4.1.2.1.
        FMSa++;
  
    if (FMSa * fout < 600000000UL)              // VCO frequency too low check and maintain an even d value
        FMSa += 2;

    // build and write values to MSNA_P3[15:8], MSNA_P3[7:0], MSNA_P1[17:16], MSNA_P1[15:8], MSNA_P1[7:0], MSNA_P2[17:16], MSNA_P2[15:8], MSNA_P2[7:0]   
    uint32_t MSNA_P1 = 128 * FMSa - 512;   // MSNA_P1 = 128 x FMSa + Floor(128*FMSb/FMSc) - 512
    uint32_t VCOA = (uint32_t) FMSa * fout;
                            
    // Output Multisynth Settings (Synthesis Stage 2)
    double fmd = (double) VCOA * CorrFact / XTAL;
    int OMSa = fmd;
    double b_c = (double)fmd - OMSa;                // Get b/c
    uint32_t OMSc = 1048575UL;
    uint32_t OMSb = (double)b_c * OMSc;
   
    // build and write values to 
    // MS0x_P3[15:8], MS0_P3[7:0], 
    // MS0_P1[17:16], MS0_P1[15:8], MS0_P1[7:0], 
    // MS0_P3[19:16] and MS0_P2[19:16], MS0_P2[15:8], MS0_P2[7:0]   
    // MSNx_P3 == OMSc;
    uint32_t MSNx_P1 = (128 * OMSa + 128 * OMSb / OMSc - 512) | (((uint32_t)1) << 20);  // 
    uint32_t MSNx_P2 = 128 * OMSb -  OMSc * (128 * OMSb / OMSc) ;                          //
    uint32_t MSNx_P3top_P2top = (((OMSc & 0x0F0000) << 4) | MSNx_P2);     // 2 top nibbles

    // Feedback Multisynth Divider and R register values     
    i2cWrite(26, BB1(OMSc));     
    i2cWrite(27, BB0(OMSc));     
    i2cWrite(28, BB2(MSNx_P1));     
    i2cWrite(29, BB1(MSNx_P1));     
    i2cWrite(30, BB0(MSNx_P1));     
    i2cWrite(31, BB2(MSNx_P3top_P2top));
    i2cWrite(32, BB1(MSNx_P2));     
    i2cWrite(33, BB0(MSNx_P2));

    // Output Multisynth Divider register values
    // Output Multisynth0, e = 0, f = 1, MS0_P2 and MSO_P3
    i2cWrite(42, 0x00);
    i2cWrite(43, 0x01);
    i2cWrite(44, BB2(MSNA_P1));  
    i2cWrite(45, BB1(MSNA_P1));  
    i2cWrite(46, BB0(MSNA_P1));
    i2cWrite(47, 0x00);
    i2cWrite(48, 0x00);
    i2cWrite(49, 0x00);
    
    //i2cWrite(16, 0x0C | 3);     // use local msynth with CLK0 drive 3
    //i2cWrite(3, 0xFF & ~1);     // Enable clock 0

    // Reset PLLA 
    delayMicroseconds(500);            // Allow registers to settle before resetting the PLL
    i2cWrite(177, 0x20);  
  }

