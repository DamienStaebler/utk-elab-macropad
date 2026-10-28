#include <FastLED.h>

// Pin definitions
#define DATA_PIN        6  // 16x16 LED Matrix DIN pin
#define UP_PIN          2  // UP button pin
#define DOWN_PIN        11 // DOWN button pin
#define LEFT_PIN        10 // LEFT button pin
#define RIGHT_PIN       3  // RIGHT button pin

#define MATRIX_WIDTH    16
#define MATRIX_HEIGHT   16
#define NUM_LEDS        (MATRIX_WIDTH * MATRIX_HEIGHT)

CRGB leds[NUM_LEDS];
CRGB leds2[NUM_LEDS];

// Button states
bool lastUpState = HIGH, lastDownState = HIGH, lastLState = HIGH, lastRState = HIGH;

// Tetris constants
#define FIELD_WIDTH     10
#define FIELD_HEIGHT    16
#define FIELD_OFFSET_X  0      // playfield starts at column 0
#define FIELD_OFFSET_Y  0

// Piece types
#define PIECE_I 0
#define PIECE_O 1
#define PIECE_T 2
#define PIECE_S 3
#define PIECE_Z 4
#define PIECE_J 5
#define PIECE_L 6
#define NUM_PIECES 7

// Piece shapes (4x4, stored in PROGMEM to save RAM)
const byte pieceShapes[NUM_PIECES][4][4] PROGMEM = {
  // I
  {{0b0000, 0b1111, 0b0000, 0b0000},
   {0b0010, 0b0010, 0b0010, 0b0010},
   {0b0000, 0b1111, 0b0000, 0b0000},
   {0b0010, 0b0010, 0b0010, 0b0010}},
  // O
  {{0b0000, 0b0110, 0b0110, 0b0000},
   {0b0000, 0b0110, 0b0110, 0b0000},
   {0b0000, 0b0110, 0b0110, 0b0000},
   {0b0000, 0b0110, 0b0110, 0b0000}},
  // T
  {{0b0000, 0b0100, 0b1110, 0b0000},
  {0b0010, 0b0110, 0b0010, 0b0000},
  {0b0000, 0b1110, 0b0100, 0b0000},
  {0b0100, 0b0110, 0b0100, 0b0000}}
  // S
  {{0b0000, 0b0110, 0b1100, 0b0000},
   {0b0100, 0b0110, 0b0010, 0b0000},
   {0b0000, 0b0110, 0b1100, 0b0000},
   {0b0100, 0b0110, 0b0010, 0b0000}},
  // Z
  {{0b0000, 0b1100, 0b0110, 0b0000},
   {0b0010, 0b0110, 0b0100, 0b0000},
   {0b0000, 0b1100, 0b0110, 0b0000},
   {0b0010, 0b0110, 0b0100, 0b0000}},
  // J
  {{0b0000, 0b1110, 0b0010, 0b0000},
   {0b0010, 0b0010, 0b0110, 0b0000},
   {0b0000, 0b1000, 0b1110, 0b0000},
   {0b0110, 0b0100, 0b0100, 0b0000}},
  // L
  {{0b0000, 0b1110, 0b1000, 0b0000},
   {0b0110, 0b0010, 0b0010, 0b0000},
   {0b0000, 0b0010, 0b1110, 0b0000},
   {0b0100, 0b0100, 0b0110, 0b0000}}
};

// Colours for each piece
const CRGB pieceColors[NUM_PIECES] = {
  CRGB::Cyan,      // I
  CRGB::Yellow,    // O
  CRGB::Magenta,   // T
  CRGB::Green,     // S
  CRGB::Red,       // Z
  CRGB::Blue,      // J
  CRGB::Orange     // L
};

// Game variables
byte field[FIELD_WIDTH][FIELD_HEIGHT];   // 0 = empty, 1..7 = piece type+1
int currentPieceType;
int currentRotation;
int currentX, currentY;                  // top‑left of piece
int nextPieceType;
int score;
bool gameOver;
unsigned long lastFallTime;
int fallDelay = 500;                      // ms per drop

// XY mapping (serpentine – same as snake)
uint16_t XY(uint8_t x, uint8_t y) {
  y = MATRIX_HEIGHT - 1 - y;
  if (y % 2 == 0) {
    return y * MATRIX_WIDTH + x;
  } else {
    return y * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - x);
  }
}

void setup() {
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(100);

  pinMode(UP_PIN, INPUT_PULLUP);
  pinMode(DOWN_PIN, INPUT_PULLUP);
  pinMode(LEFT_PIN, INPUT_PULLUP);
  pinMode(RIGHT_PIN, INPUT_PULLUP);

  randomSeed(esp_random());

  initGame();
}

void loop() {
  if (gameOver) {
    drawGameOver();
    if (checkForRestart()) {
      initGame();
    }
    return;
  }

  handleButtons();

  // Automatic falling
  if (millis() - lastFallTime >= fallDelay) {
    movePieceDown();
    lastFallTime = millis();
  }

  drawGame();
  delay(10);
}

void initGame() {
  // Clear field
  for (int x = 0; x < FIELD_WIDTH; x++) {
    for (int y = 0; y < FIELD_HEIGHT; y++) {
      field[x][y] = 0;
    }
  }
  score = 0;
  gameOver = false;
  fallDelay = 500;

  // Initial pieces
  currentPieceType = random(NUM_PIECES);
  nextPieceType = random(NUM_PIECES);
  spawnCurrentPiece();

  lastFallTime = millis();
}

void spawnCurrentPiece() {
  currentX = 3;          // centred for width 10
  currentY = 0;
  currentRotation = 0;
  if (checkCollision(currentPieceType, currentRotation, currentX, currentY)) {
    gameOver = true;
  }
}

bool checkCollision(int pieceType, int rotation, int x, int y) {
  for (int r = 0; r < 4; r++) {
    byte row = pgm_read_byte(&(pieceShapes[pieceType][rotation][r]));
    for (int c = 0; c < 4; c++) {
      if (row & (1 << (3 - c))) {               // bit set means a block
        int fx = x + c;
        int fy = y + r;
        if (fx < 0 || fx >= FIELD_WIDTH || fy < 0 || fy >= FIELD_HEIGHT) {
          return true;                           // wall collision
        }
        if (field[fx][fy] != 0) {
          return true;                           // occupied cell
        }
      }
    }
  }
  return false;
}

void lockPiece() {
  // Copy piece to field
  for (int r = 0; r < 4; r++) {
    byte row = pgm_read_byte(&(pieceShapes[currentPieceType][currentRotation][r]));
    for (int c = 0; c < 4; c++) {
      if (row & (1 << (3 - c))) {
        int fx = currentX + c;
        int fy = currentY + r;
        if (fx >= 0 && fx < FIELD_WIDTH && fy >= 0 && fy < FIELD_HEIGHT) {
          field[fx][fy] = currentPieceType + 1;   // store 1..7
        }
      }
    }
  }

  clearLines();

  // Next piece becomes current
  currentPieceType = nextPieceType;
  nextPieceType = random(NUM_PIECES);
  spawnCurrentPiece();
}

void clearLines() {
  int lines = 0;
  for (int y = FIELD_HEIGHT - 1; y >= 0; ) {
    bool full = true;
    for (int x = 0; x < FIELD_WIDTH; x++) {
      if (field[x][y] == 0) {
        full = false;
        break;
      }
    }
    if (full) {
      // Shift everything above down
      for (int yy = y; yy > 0; yy--) {
        for (int x = 0; x < FIELD_WIDTH; x++) {
          field[x][yy] = field[x][yy - 1];
        }
      }
      // Clear top line
      for (int x = 0; x < FIELD_WIDTH; x++) {
        field[x][0] = 0;
      }
      lines++;
      // Stay on same y because rows shifted down
    } else {
      y--;
    }
  }

  score += lines;                       // simple scoring: 1 point per line
  // Increase speed slightly with score
  if (score > 10) fallDelay = 400;
  if (score > 20) fallDelay = 300;
  if (score > 30) fallDelay = 200;
}

void movePieceDown() {
  if (!gameOver) {
    if (!checkCollision(currentPieceType, currentRotation, currentX, currentY + 1)) {
      currentY++;
    } else {
      lockPiece();
    }
  }
}

void handleButtons() {
  bool up = digitalRead(UP_PIN);
  bool down = digitalRead(DOWN_PIN);
  bool left = digitalRead(LEFT_PIN);
  bool right = digitalRead(RIGHT_PIN);

  // UP: rotate
  if (up == LOW && lastUpState == HIGH) {
    if (!gameOver) {
      int newRot = (currentRotation + 1) % 4;
      if (!checkCollision(currentPieceType, newRot, currentX, currentY)) {
        currentRotation = newRot;
      }
    }
  }

  // DOWN: soft drop
  if (down == LOW && lastDownState == HIGH) {
    if (!gameOver) {
      movePieceDown();
      lastFallTime = millis();          // reset timer
    }
  }

  // LEFT: move left
  if (left == LOW && lastLState == HIGH) {
    if (!gameOver) {
      if (!checkCollision(currentPieceType, currentRotation, currentX - 1, currentY)) {
        currentX--;
      }
    }
  }

  // RIGHT: move right
  if (right == LOW && lastRState == HIGH) {
    if (!gameOver) {
      if (!checkCollision(currentPieceType, currentRotation, currentX + 1, currentY)) {
        currentX++;
      }
    }
  }

  lastUpState = up;
  lastDownState = down;
  lastLState = left;
  lastRState = right;
}

void drawGame() {
  FastLED.clear();

  // ----- Draw playfield -----
  for (int x = 0; x < FIELD_WIDTH; x++) {
    for (int y = 0; y < FIELD_HEIGHT; y++) {
      if (field[x][y] != 0) {
        int idx = field[x][y] - 1;
        leds[XY(x, y)] = pieceColors[idx];
      }
    }
  }

  // ----- Draw current piece -----
  if (!gameOver) {
    for (int r = 0; r < 4; r++) {
      byte row = pgm_read_byte(&(pieceShapes[currentPieceType][currentRotation][r]));
      for (int c = 0; c < 4; c++) {
        if (row & (1 << (3 - c))) {
          int x = currentX + c;
          int y = currentY + r;
          if (x >= 0 && x < FIELD_WIDTH && y >= 0 && y < FIELD_HEIGHT) {
            leds[XY(x, y)] = pieceColors[currentPieceType];
          }
        }
      }
    }
  }

  // ----- Draw next piece (top‑right) -----
  int nextX = 11, nextY = 2;
  for (int r = 0; r < 4; r++) {
    byte row = pgm_read_byte(&(pieceShapes[nextPieceType][0][r]));
    for (int c = 0; c < 4; c++) {
      if (row & (1 << (3 - c))) {
        int x = nextX + c;
        int y = nextY + r;
        if (x < MATRIX_WIDTH && y < MATRIX_HEIGHT) {
          leds[XY(x, y)] = pieceColors[nextPieceType];
        }
      }
    }
  }

  // ----- Draw score bar (column 15) -----
  int barHeight = min(score, 16);
  for (int i = 0; i < barHeight; i++) {
    int y = MATRIX_HEIGHT - 1 - i;
    leds[XY(15, y)] = CRGB::White;
  }

  // ----- Draw vertical divider line at column 10 (playfield edge) -----
  // Use a dim white so it's visible but not distracting
  CRGB dividerColor = CRGB(64, 64, 64);   // dim grey
  for (int y = 0; y < MATRIX_HEIGHT; y++) {
    leds[XY(10, y)] = dividerColor;
  }

  FastLED.show();
}

void drawGameOver() {
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show();
}

bool checkForRestart() {
  // Hold any button for ~500 ms to restart
  unsigned long pressStart = 0;
  bool pressed = false;
  for (int i = 0; i < 100; i++) {
    bool any = (digitalRead(UP_PIN) == LOW || digitalRead(DOWN_PIN) == LOW ||
                digitalRead(LEFT_PIN) == LOW || digitalRead(RIGHT_PIN) == LOW);
    if (any) {
      if (!pressed) {
        pressed = true;
        pressStart = millis();
      } else if (millis() - pressStart > 500) {
        delay(200);
        return true;
      }
    } else {
      pressed = false;
    }
    delay(20);
  }
  return false;
}