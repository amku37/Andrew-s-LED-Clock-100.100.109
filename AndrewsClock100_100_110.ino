/******************************************************************************************
 *  NeoPixel Digital Clock – Software Vs 100.100.110 
 *  Date: November 16, 2025 

 *  Modes:
 *    1 → Fast Rainbow Chase
 *    2 → Slow Rainbow Chase (~60s)
 *    3 → Smooth HSV Sine Breathing (organic & gorgeous)
 *    4 → Fixed colors – colors rotate every hour (subtle celebration)
 *    5 → White Hours | Blue/Red Mins – roles rotate every hour
 *
 *  Features:
 *   • Zero blackout • Perfect smooth fades • Your bulletproof buttons
 *   • Hourly color shift ONLY in Modes 4 & 5 (no sweeps, no interruptions elsewhere)
 *   • Minute +/- resets seconds to 00 → perfect watch setting
 *   • EEPROM saved • 12/24h long-press toggle
 ******************************************************************************************/

#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include "RTClib.h"
#include <EEPROM.h>

// =======================================================================================
// 1. CONFIGURATION
// =======================================================================================
constexpr uint8_t LED_PIN    = 6;
constexpr uint8_t LED_COUNT  = 27;
constexpr bool    DEBUG      = true;

#if DEBUG
  #define DEBUG_PRINT(x)    Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

constexpr int DIGIT_START[4] = {0,  3, 12, 18};
constexpr int DIGIT_COUNT[4] = {3,  9,  6,  9};

// =======================================================================================
// 2. GLOBAL STATE
// =======================================================================================
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
RTC_DS3231 rtc;

bool last_lit[LED_COUNT] = {false};
bool new_lit[LED_COUNT]  = {false};

int  current_mode = 1;
constexpr int TOTAL_MODES = 5;
bool is_24_hour = false;

uint16_t hue_global = 0;
uint16_t frozen_hue = 0;
float    breath_phase = 0.0f;
unsigned long last_hue_update = 0;

// Speed per mode (ms between updates)
constexpr unsigned long MODE_INTERVALS[TOTAL_MODES] = {
  50UL,    // Mode 1: Fast chase
  234UL,   // Mode 2: Slow chase (~60s)
  1720UL,  // Mode 3: Rainbow Extra Slow
  0UL,     // Mode 4: Fixed + hourly color shift
  0UL      // Mode 5: Fixed + hourly color shift
};

const char* const MODE_NAMES[TOTAL_MODES] = {
  "1: Fast Rainbow Chase",
  "2: Slow Rainbow Chase (60s)",
  "3: Slow Rainbow Extra Slow 5min",
  "4: Red/Blue/Yellow/Green (hourly shift)",
  "5: White/Blue/Red (hourly shift)"
};

// Buttons – your legendary working order
constexpr uint8_t PIN_HOUR_UP   = 2;
constexpr uint8_t PIN_HOUR_DOWN = 3;
constexpr uint8_t PIN_MIN_UP    = 4;
constexpr uint8_t PIN_MIN_DOWN  = 5;
constexpr uint8_t PIN_MODE      = 7;

unsigned long last_debounce[5] = {0};
bool last_state[5] = {HIGH, HIGH, HIGH, HIGH, HIGH};
constexpr unsigned long DEBOUNCE_DELAY = 50UL;

unsigned long both_press_start = 0;
constexpr unsigned long HOLD_12_24_H = 10000UL;

// For Modes 4 & 5: hourly color rotation (6 patterns)
int color_rotation = 0;        // 0–5, changes every hour
int last_hour = -1;

// =======================================================================================
// Debug Print Codes
// =======================================================================================
void handleSerialCommands() {
  static bool source_code_allowed = true;
  static unsigned long boot_time = millis();

  if (millis() - boot_time > 60000) source_code_allowed = false;  // 60 sec window

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();

    if (cmd == "time") {
      DateTime now = rtc.now();
      int dh = is_24_hour ? now.hour() : ((now.hour()%12==0)?12:now.hour()%12);
      Serial.print(F("Current Time: "));
      if (dh < 10) Serial.print('0');
      Serial.print(dh); Serial.print(':');
      if (now.minute() < 10) Serial.print('0');
      Serial.print(now.minute()); Serial.print(':');
      if (now.second() < 10) Serial.print('0');
      Serial.println(now.second());
    }
    
    else if (cmd == "mode") {
      Serial.print(F("Current Mode: "));
      Serial.println(MODE_NAMES[current_mode - 1]);
    }
    
    else if (cmd == "clock") {
      Serial.println(is_24_hour ? F("24-Hour Format") : F("12-Hour Format"));
    }
    
    else if (cmd == "source_code_please" && source_code_allowed) {
      Serial.println(F("\n ==https://github.com/amku37/Andrew-s-LED-Clock-100.100.109.git==\n"));
      Serial.println(F("   ( ͡° ͜ʖ ͡°)   Download from Git hub for latest softwhere .\n"));
    }
    
    else if (cmd == "source_code_please" && !source_code_allowed) {
      Serial.println(F("Too late, pirate! Window closed. Restart to try again."));
    }
    
    else {
      Serial.println(F("Unknown command. Try: Time | Mode | Clock | source_code_please"));
    }
  }
}
// =======================================================================================
// 3. SETUP
// =======================================================================================
void setup() {
  Serial.begin(9600);
  delay(150);

  Serial.println(F("\n============================================================="));
  Serial.println(F("    Hello! Welcome to Andrew's LED Clock Vs 100.100.110"));
  Serial.println(F("           You are going to like this!  ♥"));
  Serial.println(F("============================================================="));
  Serial.println(F("SERIAL COMMANDS (case insensitive):"));
  Serial.println(F("   Time  → show current time"));
  Serial.println(F("   Mode  → show current display mode"));
  Serial.println(F("   Clock → show 12/24 hour format"));
  Serial.println(F("   source_code_please → Link to Git hub Code ... (only first 60 sec!)"));
  Serial.println(F("=============================================================\n"));

  strip.begin();
  strip.setBrightness(255);        // ← Gentle glow (change anytime)
  strip.show();

  runStartupShow();
  pinMode(PIN_HOUR_UP,   INPUT_PULLUP);
  pinMode(PIN_HOUR_DOWN, INPUT_PULLUP);
  pinMode(PIN_MIN_UP,    INPUT_PULLUP);
  pinMode(PIN_MIN_DOWN,  INPUT_PULLUP);
  pinMode(PIN_MODE,      INPUT_PULLUP);

  randomSeed(analogRead(A0));

  if (!rtc.begin()) { Serial.println(F("RTC not found!")); while (1); }
  if (rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  current_mode = EEPROM.read(0);
  if (current_mode < 1 || current_mode > TOTAL_MODES) current_mode = 1;
  is_24_hour = (EEPROM.read(1) == 1);

  DateTime now = rtc.now();
  last_hour = now.hour();
  color_rotation = now.hour() % 6;            // Start with correct rotation

  int dh = is_24_hour ? now.hour() : ((now.hour() % 12 == 0) ? 12 : now.hour() % 12);
  computeLitPattern(dh, now.minute());
  memcpy(last_lit, new_lit, sizeof(last_lit));
  forceFullUpdate();
}

// =======================================================================================
// 4. MAIN LOOP
// =======================================================================================


void loop() {
  DateTime now = rtc.now();
  int raw_hour = now.hour();
  int minute   = now.minute();
  int display_hour = is_24_hour ? raw_hour : ((raw_hour % 12 == 0) ? 12 : raw_hour % 12);

  handleSerialCommands();

  handleButtons(raw_hour, minute);

  unsigned long ms = millis();

  // Hourly color rotation for Modes 4 & 5 only
  if (raw_hour != last_hour) {
    last_hour = raw_hour;
    if (current_mode >= 4) {
      color_rotation = raw_hour % 6;
      forceFullUpdate();                 // Instantly apply new color mapping
    }
  }

  // Rainbow / breathing updates
  if (MODE_INTERVALS[current_mode - 1] > 0 && ms - last_hue_update >= MODE_INTERVALS[current_mode - 1]) {
    last_hue_update = ms;
    if (current_mode == 3) {
      breath_phase += 0.008f;
      if (breath_phase > TWO_PI) breath_phase -= TWO_PI;
      hue_global = (uint16_t)((0.5f + 0.5f * sin(breath_phase)) * 65535);
    } else {
      hue_global += 256;
    }
    forceFullUpdate();
  }

  // Minute changed → smooth fade
  static int last_minute = -1;
  if (minute != last_minute) {
    last_minute = minute;
    smoothFadeToNewTime(display_hour, minute);
  }
}

// =======================================================================================
// 5. COLOR LOGIC (WITH HOURLY ROTATION IN MODES 4 & 5) – FIXED FOR COMPILATION
// =======================================================================================
uint32_t getColorForPixel(int i) {
  int digit = digitIndexFromLed(i);

  switch (current_mode) {
    case 1: case 2:
      return strip.gamma32(strip.ColorHSV(hue_global + i * 1000));
    case 3:
      return strip.gamma32(strip.ColorHSV(hue_global + i * 200));

    case 4: {  // Fixed colors with hourly rotation
      static const uint32_t palette[4] = {
        strip.Color(255,  0,   0),   // Red
        strip.Color(  0,255,   0),   // Green
        strip.Color(  0,  0, 255),   // Blue
        strip.Color(255,255,   0)    // Yellow
      };
      int shifted = (digit + color_rotation) % 4;
      return palette[shifted];
    }

    case 5: {  // White/Blue/Red with hourly role rotation
      static const uint32_t roles[3] = {
        strip.Color(255,255,255),   // White
        strip.Color(  0,  0,255),   // Blue
        strip.Color(255,  0,  0)    // Red
      };
      if (digit < 2) return roles[color_rotation % 3];                    // Hours
      return (digit == 2) ? roles[(color_rotation + 1) % 3] : roles[(color_rotation + 2) % 3];
    }
    default: 
      return 0;
  }
}
uint32_t getColorForPixel_Frozen(int i) {
  switch (current_mode) {
    case 1: case 2: return strip.gamma32(strip.ColorHSV(frozen_hue + i * 1000));
    case 3:         return strip.gamma32(strip.ColorHSV(frozen_hue + i * 200));
    default:        return getColorForPixel(i);
  }
}

// =======================================================================================
// CORE FUNCTIONS (unchanged perfection)
// =======================================================================================
void forceFullUpdate() {
  for (int i = 0; i < LED_COUNT; i++)
    strip.setPixelColor(i, last_lit[i] ? getColorForPixel(i) : 0);
  strip.show();
}

int digitIndexFromLed(int i) {
  for (int d = 0; d < 4; d++)
    if (i >= DIGIT_START[d] && i < DIGIT_START[d] + DIGIT_COUNT[d]) return d;
  return 3;
}

void computeLitPattern(int h, int m) {
  memset(new_lit, 0, sizeof(new_lit));
  int digits[4] = {h/10, h%10, m/10, m%10};
  for (int d = 0; d < 4; d++) {
    int start = DIGIT_START[d], need = digits[d], total = DIGIT_COUNT[d];
    int idx[9]; for (int i=0; i<total; i++) idx[i] = i;
    for (int i=0; i<need && i<total; i++) {
      int j = i + random(total - i);
      int temp = idx[i]; idx[i] = idx[j]; idx[j] = temp;
      new_lit[start + idx[i]] = true;
    }
  }
}

void smoothFadeToNewTime(int h, int m) {
  computeLitPattern(h, m);
  frozen_hue = (current_mode <= 3) ? hue_global : 0;

  constexpr int STEPS = 15, DELAY_MS = 40;
  for (int s = 0; s <= STEPS; s++) {
    float out = 1.0f - s/(float)STEPS;
    float in  = s/(float)STEPS;
    for (int i = 0; i < LED_COUNT; i++) {
      bool was = last_lit[i], now = new_lit[i];
      uint32_t c = 0;
      if (was && !now) c = fadeColor(getColorForPixel_Frozen(i), out);
      else if (!was && now) c = fadeColor(getColorForPixel_Frozen(i), in);
      else if (was && now) c = getColorForPixel_Frozen(i);
      strip.setPixelColor(i, c);
    }
    strip.show();
    delay(DELAY_MS);
  }
  memcpy(last_lit, new_lit, sizeof(last_lit));
}

uint32_t fadeColor(uint32_t c, float f) {
  if (f <= 0) return 0;
  if (f >= 1) return c;
  return strip.Color(((c>>16)&0xFF)*f, ((c>>8)&0xFF)*f, (c&0xFF)*f);
}

// =======================================================================================
// 6. BULLETPROOF BUTTON HANDLING – FINAL FIXED VERSION
// =======================================================================================
void handleButtons(int &hour, int &minute) {
  unsigned long now = millis();

  bool h_up   = digitalRead(PIN_HOUR_UP)   == LOW;
  bool h_down = digitalRead(PIN_HOUR_DOWN) == LOW;
  bool m_up   = digitalRead(PIN_MIN_UP)    == LOW;
  bool m_down = digitalRead(PIN_MIN_DOWN)  == LOW;
  bool mode   = digitalRead(PIN_MODE)      == LOW;

  // 12/24h long-press
  if (h_up && h_down) {
    if (both_press_start == 0) both_press_start = now;
    else if (now - both_press_start >= HOLD_12_24_H) {
      is_24_hour = !is_24_hour;
      EEPROM.update(1, is_24_hour);
      int dh = is_24_hour ? hour : ((hour % 12 == 0) ? 12 : hour % 12);
      smoothFadeToNewTime(dh, minute);
      both_press_start = 0;
    }
  } else {
    both_press_start = 0;
  }

  bool buttons[5] = {h_up, h_down, m_up, m_down, mode};

  for (int b = 0; b < 5; b++) {
    if (buttons[b] != last_state[b]) {
      last_debounce[b] = now;
      last_state[b] = buttons[b];
    }

    if ((now - last_debounce[b]) > DEBOUNCE_DELAY && buttons[b] && last_state[b]) {
      if (b == 0) hour = (hour + 1) % 24;
      if (b == 1) hour = (hour + 23) % 24;
      if (b == 2) minute = (minute + 1) % 60;
      if (b == 3) minute = (minute + 59) % 60;
      if (b == 4) {
        current_mode = current_mode % TOTAL_MODES + 1;
        EEPROM.update(0, current_mode);
        hue_global = breath_phase = 0;
        Serial.print(F("→ Mode changed to: "));
        Serial.println(MODE_NAMES[current_mode - 1]);
      }

      // Time adjust + seconds reset
      if (b == 2 || b == 3) {
        rtc.adjust(DateTime(rtc.now().year(), rtc.now().month(), rtc.now().day(), hour, minute, 0));
      } else if (b < 4) {
        rtc.adjust(DateTime(rtc.now().year(), rtc.now().month(), rtc.now().day(), hour, minute, rtc.now().second()));
      }

      int dh = is_24_hour ? hour : ((hour % 12 == 0) ? 12 : hour % 12);
      smoothFadeToNewTime(dh, minute);
    }
  }
}

// =======================================================================================
// 7. STARTUP SHOW
// =======================================================================================
void runStartupShow() {
  uint32_t c[3] = {strip.Color(255,0,0), strip.Color(0,255,0), strip.Color(0,0,255)};
  for (int i = 0; i < 3; i++) { strip.fill(c[i]); strip.show(); delay(800); }

  unsigned long start = millis();
  uint16_t hue = 0;
  while (millis() - start < 8000) {
    hue += 256;
    for (int i = 0; i < LED_COUNT; i++)
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue + i * 1000)));
    strip.show();
    delay(15);
  }
  strip.clear(); strip.show();
}
