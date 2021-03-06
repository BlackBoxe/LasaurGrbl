/*
  sense_control.h - sensing and controlling inputs and outputs
  Part of LasaurGrbl

  Copyright (c) 2011 Stefan Hechenberger

  LasaurGrbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  LasaurGrbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
*/

#include <avr/io.h>
#include <util/delay.h>
#include <math.h>
#include <stdlib.h>
#include "sense_control.h"
#include "stepper.h"
#include "planner.h"



void sense_init() {
  //// chiller, door, (power)
  SENSE_DDR &= ~(SENSE_MASK);  // set as input pins 
  // SENSE_PORT |= SENSE_MASK;    //activate pull-up resistors 
  
  //// x1_lmit, x2_limit, y1_limit, y2_limit, z1_limit, z2_limit
  LIMIT_DDR &= ~(LIMIT_MASK);  // set as input pins
  // LIMIT_PORT |= LIMIT_MASK;    //activate pull-up resistors   
}


void control_init() {
  //// laser control
  // Setup Timer0 for a 488.28125Hz "phase correct PWM" wave (assuming a 16Mhz clock)
  // Timer0 can pwm either PD5 (OC0B) or PD6 (OC0A), we use PD6
  // TCCR0A and TCCR0B are the registers to setup Timer0
  // see chapter "8-bit Timer/Counter0 with PWM" in Atmga328 specs
  // OCR0A sets the duty cycle 0-255 corresponding to 0-100%
  // also see: http://arduino.cc/en/Tutorial/SecretsOfArduinoPWM
  DDRD |= (1 << DDD6);      // set PD6 as an output
  OCR0A = 0;              // set PWM to a 0% duty cycle
  TCCR0A |= (1 << COM0A1);  // set non-inverting mode on OC0A, PD6, Arduino pin 6
  TCCR0A |= (1 << WGM00);   // set phase correct PWM mode, has half the freq of fast PWM
  // TCCR0B |= (1 << CS00);    // prescaler to 1, PWMfreq = 16000/(2*256*1) = 31.25kHz
  // TCCR0B = _BV(CS21);;    // prescaler to 1, PWMfreq = 16000/(2*256*8) = 3.90625kHz
  TCCR0B = _BV(CS22);;    // prescaler to 1, PWMfreq = 16000/(2*256*64) = 0.48828125kHz
  
  //// air and aux assist control
  ASSIST_DDR |= (1 << AIR_ASSIST_BIT);   // set as output pin
  ASSIST_DDR |= (1 << AUX1_ASSIST_BIT);  // set as output pin
  control_air_assist(false);
  control_aux1_assist(false);
  #ifdef DRIVEBOARD
    ASSIST_DDR |= (1 << AUX2_ASSIST_BIT);  // set as output pin
    control_aux2_assist(false);
  #else  
    //// limits overwrite control
    LIMITS_OVERWRITE_DDR |= 1<<LIMITS_OVERWRITE_BIT; // define as output pin
    control_limits_overwrite(true); // do not use hardware logic to stop steppers 
  #endif  
}


void control_laser_intensity(uint8_t intensity) {
	if( intensity == 0 )  OCR0A = 0;
	else OCR0A = 255;
// OCR0A = intensity;
}



void control_air_assist(bool enable) {
  if (enable) {
    ASSIST_PORT |= (1 << AIR_ASSIST_BIT);
  } else {
    ASSIST_PORT &= ~(1 << AIR_ASSIST_BIT);
  }
}

void control_aux1_assist(bool enable) {
  if (enable) {
    ASSIST_PORT |= (1 << AUX1_ASSIST_BIT);
  } else {
    ASSIST_PORT &= ~(1 << AUX1_ASSIST_BIT);
  }  
}

#ifdef DRIVEBOARD
  void control_aux2_assist(bool enable) {
    if (enable) {
      ASSIST_PORT |= (1 << AUX2_ASSIST_BIT);
    } else {
      ASSIST_PORT &= ~(1 << AUX2_ASSIST_BIT);
    }  
  }
#else
  void control_limits_overwrite(bool enable) {
    if (enable) {
      // sinking the pin overwrites the limit stop hard logic
      LIMITS_OVERWRITE_PORT &= ~(1<<LIMITS_OVERWRITE_BIT);
    } else {
      LIMITS_OVERWRITE_PORT |= (1<<LIMITS_OVERWRITE_BIT);
    }
  }
#endif

