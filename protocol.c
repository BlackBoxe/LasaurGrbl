/*
  protocol.c - the serial protocol master control unit
  Part of Grbl

  Copyright (c) 2009-2011 Simen Svale Skogsrud
  Copyright (c) 2011 Sungeun K. Jeon  
  Copyright (c) 2011 Stefan Hechenberger 

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <avr/io.h>
#include "protocol.h"
#include "gcode.h"
#include "serial.h"
#include "print.h"
#include "settings.h"
#include "config.h"
#include <math.h>
#include "nuts_bolts.h"
#include <avr/pgmspace.h>
#include "stepper.h"
#define LINE_BUFFER_SIZE 50

static char line[LINE_BUFFER_SIZE];
static uint8_t char_counter;

static void status_message(int status_code) {
  if (status_code == 0) {
    printPgmString(PSTR("ok\r\n"));
    // for debugging, report back actual position
    // printFloat(st_get_position_x());
    // printString(", ");
    // printFloat(st_get_position_y());
    // printPgmString(PSTR("\r\n"));
  } else {
    printPgmString(PSTR("error: "));
    switch(status_code) {          
      case STATUS_BAD_NUMBER_FORMAT:
      printPgmString(PSTR("Bad number format\r\n")); break;
      case STATUS_EXPECTED_COMMAND_LETTER:
      printPgmString(PSTR("Expected command letter\r\n")); break;
      case STATUS_UNSUPPORTED_STATEMENT:
      printPgmString(PSTR("Unsupported statement\r\n")); break;
      case STATUS_FLOATING_POINT_ERROR:
      printPgmString(PSTR("Floating point error\r\n")); break;
      default:
      printInteger(status_code);
      printPgmString(PSTR("\r\n"));
    }
  }
}

void protocol_init() {
  printPgmString(PSTR("\r\nLasaurGrbl " GRBL_VERSION));
  printPgmString(PSTR("\r\n"));  
}

// Executes one line of input according to protocol
uint8_t protocol_execute_line(char *line) {
  if(line[0] == '$') {
    return(settings_execute_line(line)); // Delegate lines starting with '$' to the settings module
  } else {
    return(gc_execute_line(line));    // Everything else is gcode
  }
}

void protocol_process() {
  char c;
  uint8_t iscomment = false;
  while((c = serial_read()) != SERIAL_NO_DATA) 
  {
    if ((c == '\n') || (c == '\r')) { // End of block reached
      if (char_counter > 0) {// Line is complete. Then execute!
        line[char_counter] = 0; // terminate string
        int status = protocol_execute_line(line);
        // printInteger(serial_available());  // for debugging, reports the buffer saturation
        status_message(status);
      } else { 
        // Empty or comment line. Skip block.
        status_message(STATUS_OK); // Send status message for syncing purposes.
      }
      char_counter = 0; // Reset line buffer index
      iscomment = false; // Reset comment flag
    } else {
      if (iscomment) {
        // Throw away all comment characters
        if (c == ')') {
          // End of comment. Resume line.
          iscomment = false;
        }
      } else {
        if (c <= ' ') { 
          // Throw away whitepace and control characters
        } else if (c == '/') {
          // Disable block delete and throw away character
          // To enable block delete, uncomment following line. Will ignore until EOL.
          // iscomment = true;
        } else if (c == '(') {
          // Enable comments flag and ignore all characters until ')' or EOL.
          iscomment = true;
        } else if (char_counter >= LINE_BUFFER_SIZE-1) {
          // Throw away any characters beyond the end of the line buffer
        } else if (c >= 'a' && c <= 'z') { // Upcase lowercase
          line[char_counter++] = c-'a'+'A';
        } else {
          line[char_counter++] = c;
        }
      }
    }
  }
}
