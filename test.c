#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <avr/interrupt.h>
#include "ws2812.h"
#include "hsv_rgb.h"
#include <stdbool.h>
#include <avr/pgmspace.h>
#include "twimaster/i2cmaster.h"
#include "mcp7940_tiny.h"

const uint32_t states[10][3] PROGMEM = {
  {0b11111111, 0b11111001, 0b1111}, //0
  {0b10011000, 0b01100001, 0b1000}, //1
  {0b10011111, 0b10011111, 0b1111}, //2
  {0b10011111, 0b01101111, 0b1111}, //3
  {0b11111001, 0b01101111, 0b1000}, //4
  {0b01101111, 0b01101111, 0b1111}, //5
  {0b01101111, 0b11111111, 0b1111}, //6
  {0b10011111, 0b01100001, 0b1000}, //7
  {0b11111111, 0b11111111, 0b1111}, //8
  {0b11111111, 0b01101111, 0b1000}  //9
};
// To be used for each digit to calculate the digit's value
uint8_t digit_value;
uint8_t temp0;

#define DOUT PC7
#define SQW PD2
#define HH PB6
#define MM PB7
#define UPMIN (1<<MM)
#define UPHOUR (1<<HH)
#define BUTTONDOWN_RESET 20
volatile uint8_t buttonDown = 0;
volatile bool checkButton = false;
volatile bool updateDigits = false;
volatile bool led = false;


#define MAX_LED 128
// The state of the rainbow
uint16_t state = 0;
// reserving a byte for loop variant
uint8_t curLed;
// reserving 3*(leds) bytes for keeping the data easily accessible
uint8_t colors[MAX_LED][3];

// The start of the 10s place in hour
#define HH_0 0
// The start of the 1s place in hour
#define HH_1 20
// The start of the colon
#define COLON_0 40
// The start of the 10s place in minute
#define MM_0 48
// The start of the 1s place in minute
#define MM_1 68
// The start of the 10s place in second
#define SS_0 88
// The start of the 1s place in second
#define SS_1 108


volatile uint8_t seconds = 99;
volatile uint8_t minutes = 99;
volatile uint8_t hours = 99;

ISR(PCINT0_vect) {
  checkButton=true;
}
ISR(INT0_vect) {
  seconds++;
  led = !led;
  updateDigits = true;
}

void updateDisplay();
void loop();

int main() {
  CLKPR = 1<<CLKPCE;   // allow writes to CLKPR
  CLKPR = 0;   // disable system clock prescaler (run at full 8MHz)

  //setup PCI1 for PCINT6 and 7, for PB6 and 7
  PCMSK0 |= (1<<PCINT6) | (1<<PCINT7);
  //Setup PCINT0 to be enabled
  PCICR |= 1<<PCIE0;

  //Setup HH and MM as inputs, all other pins on port B as outputs
  DDRB = (uint8_t)( ~(UPMIN | UPHOUR));
  PORTB |= (UPMIN | UPHOUR);

  // Setup INT0 to trigger on falling edge
  EICRA = 1<<ISC01;
  // Setup INT0 to be enabled
  EIMSK = 1<<INT0;

  // Enable the display
  ws2812_init();

  // Enable I2C communication
  i2c_init();
  // Enable the RTC
  uint8_t failCode = 1;
  while(failCode) {
    failCode = mcp7940_init();
    if(failCode) {
      _delay_ms(100);
    }
  }

  mcp7940_setControlRegister( (1<<MCP7940_SQWEN) | SQWV_1HZ );

  mcp7940_setBatteryBackup(true);
  
  // Adjust this for your particular crystal, mine's 182ppm fast, so set the register to 91 (bit 7 is 0, for fast time, and then 0-6 is 91)
  mcp7940_setTrim(0b1011011);

  seconds = mcp7940_getSeconds();

  minutes = mcp7940_getMinutes();

  // bit 5 indicates whether we're in 12 or 24 hour mode
  hours = mcp7940_getHours();

  if(hours & (1<<5)) {
    //we want to be in 24 hour mode
    mcp7940_setHours( (hours&(1<<4)?12:0) + (hours&0b111), false);
    hours = mcp7940_getHours();
  }
  hours = hours & 0b11111;

  updateDigits=true;
  while(1) {
    loop();
  }
}

void loop() {
  if(seconds>59) {
    seconds = seconds % 60;
    minutes++;
    if(minutes == 60) {
      minutes = mcp7940_getMinutes();
      hours = mcp7940_getHours();
      hours = hours & 0b11111;
    }
  }
  if(!checkButton && !updateDigits) {
    _delay_ms(100);
    return;
  }
  if(checkButton) {
    uint8_t buttonState = (~PINB) & (UPMIN | UPHOUR);
    if(!buttonState) {
      checkButton=false;
      updateDigits = true;
      buttonDown = 0;
    } else {
      if(buttonDown == 0) {
        buttonDown = BUTTONDOWN_RESET;
        if(buttonState&UPMIN) {
          minutes = (minutes + 1) % 60;
          seconds = 0;
          mcp7940_setSeconds(seconds, true);
          mcp7940_setMinutes(minutes);
        }
        if(buttonState&UPHOUR) {
          hours = (hours + 1) % 24;
          mcp7940_setHours(hours, false);
        }
        updateDigits = true;
      }
      buttonDown--;
      _delay_ms(10);
    }
  }
  if(updateDigits) {
    updateDisplay();
    updateDigits=false;
  }
}

void updateDisplay() {
  state+=5;
  // hours 10s digit
  digit_value = hours / 10;
  for(curLed = HH_0; curLed < HH_1; curLed++) {
    temp0 = curLed-HH_0;
    if( !(pgm_read_byte(&states[digit_value][temp0/8]) & (1<<(temp0%8))) ) {
      colors[curLed][0] = 0;
      colors[curLed][1] = 0;
      colors[curLed][2] = 0;
      continue;
    }
    getRGB(state+(3*curLed), 50, colors[curLed]);
  }
  // hours 1s digit
  digit_value = hours % 10;
  for(curLed = HH_1; curLed < COLON_0; curLed++) {
    temp0 = curLed-HH_1;
    if( !(pgm_read_byte(&states[digit_value][temp0/8]) & (1<<(temp0%8))) ) {
      colors[curLed][0] = 0;
      colors[curLed][1] = 0;
      colors[curLed][2] = 0;
      continue;
    }
    getRGB(state+(3*curLed), 50, colors[curLed]);
  }
  // colon
  if(!led) {
    for(curLed = COLON_0; curLed < MM_0; curLed++) {
      colors[curLed][0] = 0;
      colors[curLed][1] = 0;
      colors[curLed][2] = 0;
    }
  } else {
    for(curLed = COLON_0; curLed < MM_0; curLed++) {
      getRGB(state+(3*curLed), 50, colors[curLed]);
    }
  }
  // minutes 10s digit
  digit_value = minutes / 10;
  for(curLed = MM_0; curLed < MM_1; curLed++) {
    temp0 = curLed-MM_0;
    if( !(pgm_read_byte(&states[digit_value][temp0/8]) & (1<<(temp0%8))) ) {
      colors[curLed][0] = 0;
      colors[curLed][1] = 0;
      colors[curLed][2] = 0;
      continue;
    }
    getRGB(state+(3*curLed), 50, colors[curLed]);
  }
  // minutes 1s digit
  digit_value = minutes % 10;
  for(curLed = MM_1; curLed < SS_0; curLed++) {
    temp0 = curLed-MM_1;
    if( !(pgm_read_byte(&states[digit_value][temp0/8]) & (1<<(temp0%8))) ) {
      colors[curLed][0] = 0;
      colors[curLed][1] = 0;
      colors[curLed][2] = 0;
      continue;
    }
    getRGB(state+(3*curLed), 50, colors[curLed]);
  }
  // seconds 10s digit
  digit_value = seconds / 10;
  for(curLed = SS_0; curLed < SS_1; curLed++) {
    temp0 = curLed-SS_0;
    if( !(pgm_read_byte(&states[digit_value][temp0/8]) & (1<<(temp0%8))) ) {
      colors[curLed][0] = 0;
      colors[curLed][1] = 0;
      colors[curLed][2] = 0;
      continue;
    }
    getRGB(state+(3*curLed), 50, colors[curLed]);
  }
  // seconds 1s digit
  digit_value = seconds % 10;
  for(curLed = SS_1; curLed < MAX_LED; curLed++) {
    temp0 = curLed-SS_1;
    if( !(pgm_read_byte(&states[digit_value][temp0/8]) & (1<<(temp0%8))) ) {
      colors[curLed][0] = 0;
      colors[curLed][1] = 0;
      colors[curLed][2] = 0;
      continue;
    }
    getRGB(state+(3*curLed), 50, colors[curLed]);
  }
  cli();
  for(curLed = 0; curLed < MAX_LED; curLed++) {
    ws2812_set_single(colors[curLed][0],colors[curLed][1],colors[curLed][2]);
  }
  sei();
}
