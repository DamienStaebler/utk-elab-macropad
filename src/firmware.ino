#include <Arduino.h>

const int NUM_ROWS = 3;
const int NUM_COLS = 4;

// GPIO mapping for ESP32-C3 SuperMini
const int rowPins[NUM_ROWS] = {4, 3, 2};
const int colPins[NUM_COLS] = {1, 0, 10, 20};

// Rotary Encoder Pins
const int ENCODER_CLK = 6; // GPIO6
const int ENCODER_DT  = 7; // GPIO7

volatile long encoderPosition = 0;
long lastEncoderPosition = 0;

volatile uint8_t encoderState = 0;
volatile int8_t stateCount = 0;

const int8_t KNOB_TABLE[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

// Eager Matrix Debouncing Variables
bool currentStates[NUM_ROWS][NUM_COLS] = {false};
unsigned long lastDebounceTime[NUM_ROWS][NUM_COLS] = {0};
const unsigned long DEBOUNCE_LOCKOUT_MS = 8; // Instant trigger, 8ms bounce lockout

// Interrupt Service Routine for Encoder State Machine
void IRAM_ATTR handleEncoderInterrupt() {
  uint8_t clk_val = digitalRead(ENCODER_CLK);
  uint8_t dt_val = digitalRead(ENCODER_DT);
  uint8_t curr_state = (clk_val << 1) | dt_val;

  uint8_t table_idx = ((encoderState & 0x03) << 2) | curr_state;
  int8_t step_dir = KNOB_TABLE[table_idx];

  if (step_dir != 0) {
    stateCount += step_dir;
    // Standard EC11 encoders pass through 4 state steps per physical detent click
    if (stateCount >= 4) {
      encoderPosition++;
      stateCount = 0;
    } else if (stateCount <= -4) {
      encoderPosition--;
      stateCount = 0;
    }
  }
  encoderState = curr_state;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Matrix setup: Rows high-impedance, Columns pulled high
  for (int row_idx = 0; row_idx < NUM_ROWS; row_idx++) {
    pinMode(rowPins[row_idx], INPUT);
  }
  for (int col_idx = 0; col_idx < NUM_COLS; col_idx++) {
    pinMode(colPins[col_idx], INPUT_PULLUP);
  }

  // Rotary Encoder setup
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);

  // Seed initial encoder state
  encoderState = (digitalRead(ENCODER_CLK) << 1) | digitalRead(ENCODER_DT);

  // Attach CHANGE interrupts to both channels for full quadrature decoding
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), handleEncoderInterrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_DT), handleEncoderInterrupt, CHANGE);
}

void loop() {
  unsigned long now = millis();

  // 1. Fast Matrix Scan with Eager (Instant) Debouncing
  for (int row_idx = 0; row_idx < NUM_ROWS; row_idx++) {
    pinMode(rowPins[row_idx], OUTPUT);
    digitalWrite(rowPins[row_idx], LOW);
    delayMicroseconds(1); // Minimal settling delay for 1N4148 diodes

    for (int col_idx = 0; col_idx < NUM_COLS; col_idx++) {
      bool is_pressed = (digitalRead(colPins[col_idx]) == LOW);

      // Fire event immediately when state changes, then enforce lockout
      if (is_pressed != currentStates[row_idx][col_idx] && (now - lastDebounceTime[row_idx][col_idx] >= DEBOUNCE_LOCKOUT_MS)) {
        currentStates[row_idx][col_idx] = is_pressed;
        lastDebounceTime[row_idx][col_idx] = now;

        int button_id = (row_idx * NUM_COLS) + col_idx + 1;
        if (is_pressed) {
          Serial.printf("BTN:PRESS:%d\n", button_id);
        } else {
          Serial.printf("BTN:RELEASE:%d\n", button_id);
        }
      }
    }
    pinMode(rowPins[row_idx], INPUT); // Return row to high-impedance
  }

  // 2. Rotary Encoder Output Report (Atomic Read)
  long current_pos;
  noInterrupts();
  current_pos = encoderPosition;
  interrupts();

  if (current_pos != lastEncoderPosition) {
    long delta = current_pos - lastEncoderPosition;

    if (delta > 0) {
      for (long i = 0; i < delta; i++) Serial.println("ENC:+1");
    } else if (delta < 0) {
      for (long i = 0; i < -delta; i++) Serial.println("ENC:-1");
    }

    lastEncoderPosition = current_pos;
  }

  delay(1);
}