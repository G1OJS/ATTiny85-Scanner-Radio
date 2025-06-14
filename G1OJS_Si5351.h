#ifndef G1OJS_Si5351_H_
#define G1OJS_Si5351_H_

#include "Arduino.h"
#include "Wire.h"

class G1OJS_Si5351{
  public:
	void set_freq(uint32_t fout);
};

#endif