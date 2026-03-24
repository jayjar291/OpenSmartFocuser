

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "config.h"

// Display: 135x240 panel in horizontal orientation = 240x135 logical
static constexpr uint16_t LCD_WIDTH = 240;
static constexpr uint16_t LCD_HEIGHT = 135;
static constexpr uint8_t LCD_ROTATION = 3;  // 180-degree rotation

Adafruit_ST7789 tft(PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);

// Motor state tracking
struct MotorState {
  int32_t position;          // Current position in steps
  uint16_t current_speed;    // Current step frequency (Hz)
  bool moving;               // Is motor actively stepping
  bool direction;            // true = UP, false = DOWN
  uint32_t steps_taken;      // Total steps executed
  uint32_t last_step_time;   // Microseconds of last step
  bool enabled;              // Motor enable/disable state
  int32_t last_displayed_pos; // Track previous position for display updates
};

static MotorState motor = {0, 0, false, true, 0, 0, false, 0};

// Button state tracking
struct ButtonState {
  uint8_t pin;
  bool currently_pressed;
  bool last_pressed;
  uint32_t debounce_time;
  bool debounced;
  bool edge_detected;        // Falling edge detection for SELECT
};

static ButtonState buttons[] = {
  {PIN_BUTTON_UP, false, false, 0, false, false},
  {PIN_BUTTON_DOWN, false, false, 0, false, false},
  {PIN_BUTTON_SELECT, false, false, 0, false, false},
  {PIN_BUTTON_ENDSTOP, false, false, 0, false, false},
};

static constexpr size_t BUTTON_COUNT = sizeof(buttons) / sizeof(buttons[0]);

static void initPins() {
  // Button inputs with pull-ups
  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
  }
  
  // Motor control outputs
  pinMode(PIN_TMC_STEP, OUTPUT);
  digitalWrite(PIN_TMC_STEP, LOW);
  
  pinMode(PIN_TMC_DIR, OUTPUT);
  digitalWrite(PIN_TMC_DIR, LOW);
  
  pinMode(PIN_TMC_ENABLE, OUTPUT);
  digitalWrite(PIN_TMC_ENABLE, HIGH);  // Enable motor disabled by default (active low)
}

static void initDisplay() {
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  SPI.begin(PIN_LCD_SCK, PIN_LCD_MISO, PIN_LCD_MOSI, PIN_LCD_CS);
  tft.init(135, 240);  // Physical panel dimensions
  tft.setRotation(LCD_ROTATION);  // 180-degree rotation
  tft.enableDisplay(true);
  tft.enableSleep(false);
}

static bool buttonPressed(uint8_t pin) {
  return digitalRead(pin) == LOW;
}

static void updateButtonStates() {
  uint32_t now = millis();
  
  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    ButtonState &btn = buttons[i];
    btn.currently_pressed = buttonPressed(btn.pin);
    
    // Debounce: if state changes, start timer; if stable after debounce period, update
    if (btn.currently_pressed != btn.last_pressed) {
      btn.debounce_time = now;
      btn.last_pressed = btn.currently_pressed;
    } else if (now - btn.debounce_time >= BUTTON_DEBOUNCE_MS) {
      bool old_debounced = btn.debounced;
      btn.debounced = btn.currently_pressed;
      // Detect falling edge (press)
      if (old_debounced && !btn.debounced) {
        btn.edge_detected = true;
      } else {
        btn.edge_detected = false;
      }
    }
  }
}

static bool isButtonDown(size_t index) {
  if (index < BUTTON_COUNT) {
    return buttons[index].debounced;
  }
  return false;
}

static bool isButtonPressed(size_t index) {
  if (index < BUTTON_COUNT) {
    return buttons[index].edge_detected;
  }
  return false;
}

static void motorStep() {
  // Generate a STEP pulse
  digitalWrite(PIN_TMC_STEP, HIGH);
  delayMicroseconds(MOTOR_PULSE_WIDTH_US);
  digitalWrite(PIN_TMC_STEP, LOW);
  
  // Update position
  if (motor.direction) {
    motor.position++;
  } else {
    motor.position--;
  }
  motor.steps_taken++;
  motor.last_step_time = micros();
}

static void updateMotor() {
  uint32_t now_us = micros();
  
  // Check for emergency stop
  if (isButtonDown(3)) {  // ENDSTOP button
    motor.moving = false;
    return;
  }
  
  // Handle SELECT button: toggle motor enable
  if (isButtonPressed(2)) {  // SELECT button pressed
    motor.enabled = !motor.enabled;
    if (motor.enabled) {
      digitalWrite(PIN_TMC_ENABLE, LOW);   // Enable (active low)
    } else {
      digitalWrite(PIN_TMC_ENABLE, HIGH);  // Disable
      motor.moving = false;
    }
  }
  
  // Only allow movement if motor is enabled
  if (!motor.enabled) {
    motor.moving = false;
    motor.current_speed = 0;
    return;
  }
  
  // Handle UP button (momentary)
  if (isButtonDown(0)) {  // UP button held
    motor.direction = true;
    digitalWrite(PIN_TMC_DIR, HIGH);
    motor.moving = true;
  } else if (isButtonDown(1)) {  // DOWN button held
    motor.direction = false;
    digitalWrite(PIN_TMC_DIR, LOW);
    motor.moving = true;
  } else {
    // Neither button pressed - stop movement
    motor.moving = false;
    motor.current_speed = 0;
  }
  
  // Execute stepping if motor should be moving
  if (motor.moving) {
    uint32_t step_period = 1000000 / MOTOR_MAX_SPEED_HZ;  // Microseconds
    if (now_us - motor.last_step_time >= step_period) {
      motorStep();
      motor.current_speed = MOTOR_MAX_SPEED_HZ;
    }
  } else {
    motor.current_speed = 0;
  }
}

static void drawFullScreen() {
  tft.fillScreen(ST77XX_BLACK);
  
  // Title
  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(8, 4);
  tft.print("Focuser");
  
  // Motor Status section
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setCursor(8, 24);
  tft.print("Pos: ");
  tft.print(motor.position);
  
  tft.setCursor(8, 34);
  motor.last_displayed_pos = motor.position;
}

static void updateMotorDisplay() {
  // Only update position if it changed
  if (motor.position != motor.last_displayed_pos) {
    tft.fillRect(8, 24, 120, 8, ST77XX_BLACK);  // Clear position area
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setCursor(8, 24);
    tft.print("Pos: ");
    tft.print(motor.position);
    motor.last_displayed_pos = motor.position;
  }
  
  // Motor direction
  tft.fillRect(8, 34, 60, 8, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setCursor(8, 34);
  tft.print("Dir: ");
  tft.print(motor.direction ? "UP " : "DOWN");
  
  // Motor state
  tft.fillRect(8, 44, 80, 8, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(8, 44);
  tft.print("Mov: ");
  tft.print(motor.moving ? "YES" : "NO ");
  
  // Motor enable state
  uint16_t enableColor = motor.enabled ? ST77XX_GREEN : ST77XX_RED;
  tft.fillRect(8, 54, 80, 8, ST77XX_BLACK);
  tft.setTextColor(enableColor, ST77XX_BLACK);
  tft.setCursor(8, 54);
  tft.print("Enable: ");
  tft.print(motor.enabled ? "ON " : "OFF");
  
  // Speed
  tft.fillRect(8, 64, 80, 8, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(8, 64);
  tft.print("Speed: ");
  tft.print(motor.current_speed);
  tft.print("Hz");
}

static void updateButtonDisplay() {
  // Button States - more compact horizontal layout
  tft.fillRect(8, 80, 220, 20, ST77XX_BLACK);  // Clear button area
  
  tft.setTextSize(1);
  
  // UP button
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setCursor(8, 80);
  tft.print("UP");
  uint16_t upColor = isButtonDown(0) ? ST77XX_GREEN : ST77XX_RED;
  tft.setTextColor(upColor, ST77XX_BLACK);
  tft.setCursor(28, 80);
  tft.print(isButtonDown(0) ? "P" : "R");
  
  // DOWN button
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setCursor(38, 80);
  tft.print("DN");
  uint16_t downColor = isButtonDown(1) ? ST77XX_GREEN : ST77XX_RED;
  tft.setTextColor(downColor, ST77XX_BLACK);
  tft.setCursor(58, 80);
  tft.print(isButtonDown(1) ? "P" : "R");
  
  // SELECT button
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setCursor(68, 80);
  tft.print("SEL");
  uint16_t selColor = isButtonDown(2) ? ST77XX_GREEN : ST77XX_RED;
  tft.setTextColor(selColor, ST77XX_BLACK);
  tft.setCursor(110, 80);
  tft.print(isButtonDown(2) ? "P" : "R");
  
  // ESTOP button
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setCursor(120, 80);
  tft.print("STOP");
  uint16_t estopColor = isButtonDown(3) ? ST77XX_RED : ST77XX_GREEN;
  tft.setTextColor(estopColor, ST77XX_BLACK);
  tft.setCursor(172, 80);
  tft.print(isButtonDown(3) ? "!!!" : " OK");
  
  // Stats
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(8, 100);
  tft.print("Steps: ");
  tft.print(motor.steps_taken);
  
  tft.setCursor(8, 115);
  tft.print("TMC: GPIO15->TX, GPIO16->RX");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("OpenSmartFocuser - Motor Control");
  
  initPins();
  initDisplay();
  
  drawFullScreen();
  updateMotorDisplay();
  updateButtonDisplay();
}

void loop() {
  updateButtonStates();
  updateMotor();
  
  // Update display at ~50Hz (no full redraws, only regional updates)
  static uint32_t last_update = 0;
  uint32_t now = millis();
  if (now - last_update >= 20) {
    updateMotorDisplay();
    updateButtonDisplay();
    last_update = now;
  }
  
  delay(2);
}
