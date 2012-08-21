/*
  serial.c - Low level functions for sending and recieving bytes via the serial port.
  Part of LasaurGrbl

  Copyright (c) 2009-2011 Simen Svale Skogsrud
  Copyright (c) 2011 Sungeun K. Jeon
  Copyright (c) 2011 Stefan Hechenberger

  Inspired by the wiring_serial module by David A. Mellis which
  used to be a part of the Arduino project.
   
  LasaurGrbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  LasaurGrbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
*/

#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <math.h>
#include <avr/pgmspace.h>
#include "serial.h"
#include "config.h"
#include "stepper.h"
#include "gcode.h"


#define RX_BUFFER_SIZE 255
#define TX_BUFFER_SIZE 128

uint8_t rx_buffer[RX_BUFFER_SIZE];
volatile uint8_t rx_buffer_head = 0;
volatile uint8_t rx_buffer_tail = 0;

#define RX_MIN_OPEN_SLOTS 127  // when to trigger XONXOFF event
volatile uint8_t rx_buffer_open_slots = RX_BUFFER_SIZE;
volatile uint8_t xoff_flag = 0;
volatile uint8_t xon_flag = 0;
volatile uint8_t xon_remote_state = 0;

uint8_t tx_buffer[TX_BUFFER_SIZE];
volatile uint8_t tx_buffer_head = 0;
volatile uint8_t tx_buffer_tail = 0;

static void set_baud_rate(long baud) {
  uint16_t UBRR0_value = ((F_CPU / 16 + baud / 2) / baud - 1);
	UBRR0H = UBRR0_value >> 8;
	UBRR0L = UBRR0_value;
}

void serial_init() {
  set_baud_rate(BAUD_RATE);
  
	/* baud doubler off  - Only needed on Uno XXX */
  UCSR0A &= ~(1 << U2X0);
          
	// enable rx and tx
  UCSR0B |= 1<<RXEN0;
  UCSR0B |= 1<<TXEN0;
	
	// enable interrupt on complete reception of a byte
  UCSR0B |= 1<<RXCIE0;
	  
	// defaults to 8-bit, no parity, 1 stop bit
	
	// send a XON to indicate device is ready to receive
	xon_flag = 1;
	UCSR0B |=  (1 << UDRIE0);  // enable tx interrupt
	
  printPgmString(PSTR("# LasaurGrbl " LASAURGRBL_VERSION));
  printPgmString(PSTR("\n")); 	
}

void serial_write(uint8_t data) {
  // Calculate next head
  uint8_t next_head = tx_buffer_head + 1;
  if (next_head == TX_BUFFER_SIZE) { next_head = 0; }

  // Wait until there's a space in the buffer
  while (next_head == tx_buffer_tail) { sleep_mode(); };

  // Store data and advance head
  tx_buffer[tx_buffer_head] = data;
  tx_buffer_head = next_head;
  
  // Enable Data Register Empty Interrupt to make sure tx-streaming is running
	UCSR0B |=  (1 << UDRIE0); 
}

// Data Register Empty Interrupt handler
SIGNAL(USART_UDRE_vect) {
  // temporary tx_buffer_tail (to optimize for volatile)
  uint8_t tail = tx_buffer_tail;
  
  if (xoff_flag) {
    UDR0 = CHAR_XOFF;  //send XOFF
    xoff_flag = 0;
    xon_remote_state = 0;
  } else if (xon_flag) {
    UDR0 = CHAR_XON;  //send XON
    xon_flag = 0;
    xon_remote_state = 1;
  } else {
    // Send a byte from the buffer	
    UDR0 = tx_buffer[tail];

    // Update tail position
    tail++;
    if (tail == TX_BUFFER_SIZE) { tail = 0; }
    
    tx_buffer_tail = tail;
  }
  
  // Turn off Data Register Empty Interrupt to stop tx-streaming if this concludes the transfer
  if (tail == tx_buffer_head) { UCSR0B &= ~(1 << UDRIE0); }  
}

uint8_t serial_read() {
	if (rx_buffer_tail == rx_buffer_head) {
		return SERIAL_NO_DATA;
	} else {
		uint8_t data = rx_buffer[rx_buffer_tail];
		rx_buffer_tail++;
    rx_buffer_open_slots++;
    if (rx_buffer_tail == RX_BUFFER_SIZE) { rx_buffer_tail = 0; } 
    
    if (xon_remote_state == 0) {  // generate flow control event
      if (rx_buffer_open_slots > RX_MIN_OPEN_SLOTS) {
        xon_flag = 1;
      	UCSR0B |=  (1 << UDRIE0);  // Enable Data Register Empty Interrupt      
      }
    }
  
		return data;
	}
}

uint8_t serial_available() {
  return RX_BUFFER_SIZE - rx_buffer_open_slots;
}

// Rx Interrupt, called whenever a new byte is in UDR0
SIGNAL(USART_RX_vect) {
  uint8_t data = UDR0;
  if (data == CHAR_STOP) {
    // special stop character, bypass buffer
    stepper_request_stop(STATUS_SERIAL_STOP_REQUEST);
  } else if (data == CHAR_RESUME) {
    // special resume character, bypass buffer
    stepper_stop_resume();
  } else {
    uint8_t next_head = rx_buffer_head + 1;
    if (next_head == RX_BUFFER_SIZE) { next_head = 0; }

    if (next_head == rx_buffer_tail) {
      // buffer is full, other side sent too much data
      stepper_request_stop(STATUS_BUFFER_OVERFLOW);
    } else {
      rx_buffer[rx_buffer_head] = data;
      rx_buffer_head = next_head;
      rx_buffer_open_slots--;
    
      if (xon_remote_state == 1) {  // generate flow control event
        if (rx_buffer_open_slots <= RX_MIN_OPEN_SLOTS) {
          xoff_flag = 1;
          UCSR0B |=  (1 << UDRIE0);  // Enable Data Register Empty Interrupt
        }
      } 
    }
  }
}




void printString(const char *s) {
  while (*s) {
    serial_write(*s++);
  }
}

// Print a string stored in PGM-memory
void printPgmString(const char *s) {
  char c;
  while ((c = pgm_read_byte_near(s++))) {
    serial_write(c);
  }
}

void printIntegerInBase(unsigned long n, unsigned long base) {
  unsigned char buf[8 * sizeof(long)]; // Assumes 8-bit chars.
  unsigned long i = 0;

  if (n == 0) {
    serial_write('0');
    return;
  }

  while (n > 0) {
    buf[i++] = n % base;
    n /= base;
  }

  for (; i > 0; i--) {
    serial_write(buf[i - 1] < 10 ?
    '0' + buf[i - 1] :
    'A' + buf[i - 1] - 10);
  }
}

void printInteger(long n) {
  if (n < 0) {
    serial_write('-');
    n = -n;
  }

  printIntegerInBase(n, 10);
}

// A very simple
void printFloat(double n) {
  if (n < 0) {
    serial_write('-');
    n = -n;
  }
  n += 0.5/1000; // Add rounding factor
 
  long integer_part;
  integer_part = (int)n;
  printIntegerInBase(integer_part,10);
  
  serial_write('.');
  
  n -= integer_part;
  int decimals = 3;
  uint8_t decimal_part;
  while(decimals-- > 0) {
    n *= 10;
    decimal_part = (int) n;
    serial_write('0'+decimal_part);
    n -= decimal_part;
  }
}

