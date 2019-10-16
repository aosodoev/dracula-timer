#include <avr/wdt.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <util/delay.h>

#define CONTROL_ACTIVE_HIGH
// #define LED_ACTIVE_HIGH

#define BUTTON _BV(PB0)
#define LED _BV(PB1)
#define CONTROL _BV(PB2)

#ifdef CONTROL_ACTIVE_HIGH
#define BUTTON_PRESSED BUTTON
#else
#define BUTTON_PRESSED 0
#endif

uint16_t EEMEM count = 25*60;

volatile uint16_t countdown = 0;
volatile char pin_change = 0;
char calibration = 0;

void setup() {
  MCUSR &= ~_BV(WDRF);
  wdt_disable();
  PORTB = ~(LED | CONTROL | BUTTON_PRESSED); // enable all pullups except LED and TRIGGER
}

void start_timer() {
  countdown = eeprom_read_word(&count);
  // countdown = 1271; // 25*60; 1271 - corrected amount of blinks
  cli();
  wdt_reset();
  MCUSR &= ~_BV(WDRF);
  WDTCR |= _BV(WDCE) | _BV(WDE);
  WDTCR = (WDTCR | _BV(WDTIE) | WDTO_1S) & ~_BV(WDE);
  sei();
}

void stop_timer() {
  cli();
  countdown = 0;
  calibration = 0;
  wdt_reset();
  MCUSR &= ~_BV(WDRF);
  WDTCR = (WDTCR | _BV(WDTIF)) & ~_BV(WDTIE);
  wdt_disable();
  sei();
}

void sleep() {

  pin_change = 0;

  power_all_disable();
  cli();
  GIMSK |= _BV(PCIE);                     // Enable Pin Change Interrupts
  PCMSK = _BV(PCINT0);                   // Use PB0 as interrupt pin
  GIFR = _BV(PCIF);    // reset pin change interrupt flag

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();                         // Sets the Sleep Enable bit in the MCUCR Register (SE BIT)
  sei();                                  // Enable interrupts
  sleep_cpu();                            // sleep
  cli();                                  // Disable interrupts
  PCMSK = 0;                // Turn off pin change interrupts
  sleep_disable();                        // Clear SE bit
  sei();                                  // Enable interrupts
}

inline void led_on() {
#ifdef LED_ACTIVE_HIGH
  PORTB |= LED;
#endif
  DDRB |= LED;
}

inline void led_off() {
  DDRB &= ~LED;
#ifdef LED_ACTIVE_HIGH
  PORTB &= ~LED;
#endif
}

void blink3x() {
  for (uint8_t i = 0; i < 3; i++) {
    led_on();
    _delay_ms(50);
    led_off();
    _delay_ms(150);
  }
}

int main() {
  setup();
  while (1) {
    sleep();
    if (pin_change) {
      // if have been awakened by pin change interrupt, not wdt timer or loud neighbours
      // probably should check flag in a register instead of setting variable in ISR, but too lazy to read datasheet
      
      uint8_t last_reading = BUTTON;
      for (uint8_t i = 0; i<10; i++) {
	uint8_t reading = PINB & BUTTON;
	if (reading != last_reading) {
	  last_reading = reading;
	  i = 0;
	}
	_delay_ms(5);
      }
      
      if (last_reading != BUTTON_PRESSED) { // button was released, skip the rest and go back to sleep
	continue;
      }
      
      /* ========== button was in fact pressed, proceed ========== */
      
      if (countdown) { // if timer is running, stop
	if (calibration) { // if it was calibration run, update stored counter value
	  uint16_t seconds = eeprom_read_word(&count) - countdown;
	  if ((seconds > 200) && (seconds < 400)) { // burn only reasonable number
	    eeprom_update_word(&count, seconds*5);
	  }
	}
	stop_timer();
	 // "terminator death" fade out
	for (uint8_t i = 0; i < 100; i++) {
	  led_off();
	  for (uint8_t j = 0; j < 10; j++) {
	    _delay_ms(1);
	    if (j == i / 10)
	      led_on();
	  }
	}
	led_off();
      } else { // start timer
	blink3x();
	start_timer();
	uint16_t long_press = 5000/10;
	while (long_press && ((PINB & BUTTON) == BUTTON_PRESSED)) {
	  _delay_ms(10);
	  long_press--;
	  if (!long_press) {
	    calibration = 1; // start 5-minute calibration
	    blink3x();
	  }
	}
      }
    }
  }
  return 0;
}

ISR(WDT_vect) {
  if (countdown) {
    countdown--;
    led_on();
    if (!countdown) {
      stop_timer();
#ifdef CONTROL_ACTIVE_HIGH
      PORTB |= CONTROL;
#endif
      DDRB |= CONTROL;
      _delay_ms(50); // longer delay to trigger toy
    } else {
      _delay_ms(20); // regular LED blink
    }
    led_off();
    if (!countdown) {
      DDRB &= ~CONTROL;
#ifdef CONTROL_ACTIVE_HIGH
      PORTB &= ~CONTROL;
#endif
    }
  }
}

ISR(PCINT0_vect) {
  pin_change = 1;
}
