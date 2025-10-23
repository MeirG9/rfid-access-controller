/* ============================================================================
 * File: Nano.ino  (ATmega328P / Arduino Nano/Uno)
 * Purpose     : PWM "breathing" LED on OC2B with light-adaptive tempo via ADC;
 *               Turbo mode via INT0 to stream RFID UID frames between Nano<->ESP.
 * Peripherals : Timer2 (Fast PWM on OC2B), Timer1 (CTC tick), ADC0 (free-run),
 *               INT0 (button), SoftwareSerial for RDM6300 reader.
 * Wiring      :
 *   - D3 (OC2B) → LED (PWM breathing)
 *   - A0 (ADC0) → LDR divider
 *   - D2 (INT0) → Turbo button (active-low, INPUT_PULLUP)
 *   - D8 (Soft RX)  ← RDM6300 TX
 *   - D7 (Soft TX)  → (dummy)
 *   - D5 (OK LED), D6 (ERR LED)
 * UART        : HardwareSerial (9600) to ESP8266 (STRICT: no prints from Nano).
 * Notes       :
 *   - ADC interrupt toggles Timer1 OCR between FAST/SLOW thresholds (ambient light).
 *   - Turbo: disables ADC ISR, speeds breathing, and forwards bytes Nano<->ESP.

 * ========================================================================== */
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <avr/interrupt.h>

constexpr uint8_t PIN_PWM   = 3;   // OC2B (D3)
constexpr uint8_t PIN_LDR   = A0;  // ADC0
constexpr uint8_t PIN_BTN   = 2;   // INT0 (active-low, INPUT_PULLUP)
constexpr uint8_t PIN_RDMRX = 8;   // SoftSerial RX (RDM6300 TX)
constexpr uint8_t PIN_RDMTX = 7;   // SoftSerial TX (dummy)
constexpr uint8_t PIN_OK    = 5;   // Green LED
constexpr uint8_t PIN_ERR   = 6;   // Red LED

constexpr uint16_t OCR_FAST  = 499;    // ~2.0 ms (presc 64)
constexpr uint16_t OCR_SLOW  = 1999;   // ~8.0 ms
constexpr uint16_t OCR_TURBO = 100;    // fast breathing while Turbo held
constexpr uint16_t TH_BRIGHT = 680;    // ADC threshold
constexpr uint8_t  DUTY_MIN  = 5, DUTY_MAX = 250, DUTY_STEP = 1;

volatile uint8_t duty = DUTY_MIN;
volatile int8_t  dir  = +1;
volatile uint8_t turbo = 0, turbo_pending = 0;

SoftwareSerial rdm(PIN_RDMRX, PIN_RDMTX);
HardwareSerial& esp = Serial;

static inline void setTick(uint16_t v) { uint8_t s=SREG; cli(); OCR1A=v; SREG=s; }

static void setupPWM() {
  TCCR2A = (1<<COM2B1)|(1<<WGM21)|(1<<WGM20); // Fast PWM, non-inverting on OC2B
  TCCR2B = (1<<CS22);                          // prescaler 64 → ~976 Hz
  pinMode(PIN_PWM, OUTPUT);
  OCR2B = duty;
}

static void setupTick() {
  TCCR1A = 0;
  TCCR1B = (1<<WGM12)|(1<<CS11)|(1<<CS10);     // CTC, presc 64
  TIMSK1 = (1<<OCIE1A);                        // enable COMPA ISR
  setTick(OCR_FAST);
}

ISR(TIMER1_COMPA_vect) {
  int16_t n = duty + dir * DUTY_STEP;
  if (n >= DUTY_MAX) { n = DUTY_MAX; dir=-1; }
  else if (n <= DUTY_MIN) { n = DUTY_MIN; dir=+1; }
  duty = (uint8_t)n;
  OCR2B = duty;
}

static void setupADC() {
  ADMUX  = (1<<REFS0); // AVcc, ADC0
  ADCSRA = (1<<ADEN)|(1<<ADATE)|(1<<ADIE)|(1<<ADPS2)|(1<<ADPS1)|(1<<ADPS0); // free-run, /128
  ADCSRB = 0; DIDR0 |= (1<<ADC0D);
  ADCSRA |= (1<<ADSC);
}
ISR(ADC_vect) { uint16_t a = ADC; setTick(a > TH_BRIGHT ? OCR_FAST : OCR_SLOW); }

static void setupINT0() {
  pinMode(PIN_BTN, INPUT_PULLUP);
  EICRA = (1<<ISC00);  // logical change
  EIFR  = (1<<INTF0);  // clear pending
  EIMSK = (1<<INT0);   // enable INT0
  turbo = ((PIND & (1<<PIND2))==0);
  turbo_pending = 1;
}
ISR(INT0_vect) { turbo = ((PIND & (1<<PIND2))==0); turbo_pending=1; }

static inline void applyTurbo() {
  if (!turbo) { ADCSRA |= (1<<ADIE); rdm.stopListening(); setTick(ADC>TH_BRIGHT?OCR_FAST:OCR_SLOW); }
  else        { ADCSRA &= ~(1<<ADIE); rdm.listen();       setTick(OCR_TURBO); }
}

void setup() {
  esp.begin(9600);            // HW UART to ESP (STRICT: no prints)
  pinMode(PIN_RDMTX, OUTPUT);
  rdm.begin(9600); rdm.stopListening();
  pinMode(PIN_OK,OUTPUT); pinMode(PIN_ERR,OUTPUT);
  setupPWM(); setupTick(); setupADC(); setupINT0(); sei();
}

void loop() {
  if (turbo_pending) { applyTurbo(); turbo_pending=0; }

  if (turbo) {
    while (rdm.available()) { esp.write((uint8_t)rdm.read()); }
    while (esp.available()) {
      int r=esp.read();
      digitalWrite(PIN_OK,  r==0x01);
      digitalWrite(PIN_ERR, r==0x00);
    }
  } else {
    digitalWrite(PIN_OK,LOW); digitalWrite(PIN_ERR,LOW);
  }
}
