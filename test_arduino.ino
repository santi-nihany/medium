#include <IRremote.hpp>

#define IR_RECEIVE_PIN 2

// Pines de LEDs
const uint8_t ledPins[] = {3, 4, 5, 6};
const uint8_t LED_COUNT = 4;

// Estados posibles
enum LedMode {
  ALL_ON,
  BLINK_ALL,
  SEQ_UP,
  SEQ_DOWN,
  IDLE
};

LedMode currentMode = IDLE;

// Variables de temporización
unsigned long lastMillis = 0;
uint8_t ledIndex = 0;
bool blinkState = false;

// ---------------- FUNCIONES AUXILIARES ----------------

void allLedsOff() {
  for (uint8_t i = 0; i < LED_COUNT; i++) {
    digitalWrite(ledPins[i], LOW);
  }
}

void allLedsOn() {
  for (uint8_t i = 0; i < LED_COUNT; i++) {
    digitalWrite(ledPins[i], HIGH);
  }
}

void handleLedModes() {
  unsigned long now = millis();

  switch (currentMode) {

    case ALL_ON:
      allLedsOn();
      break;

    case BLINK_ALL:
      if (now - lastMillis >= 200) {
        lastMillis = now;
        blinkState = !blinkState;
        for (uint8_t i = 0; i < LED_COUNT; i++) {
          digitalWrite(ledPins[i], blinkState);
        }
      }
      break;

    case SEQ_UP:
      if (now - lastMillis >= 200) {
        lastMillis = now;
        allLedsOff();
        digitalWrite(ledPins[ledIndex], HIGH);
        ledIndex = (ledIndex + 1) % LED_COUNT;
      }
      break;

    case SEQ_DOWN:
      if (now - lastMillis >= 200) {
        lastMillis = now;
        allLedsOff();
        digitalWrite(ledPins[LED_COUNT - 1 - ledIndex], HIGH);
        ledIndex = (ledIndex + 1) % LED_COUNT;
      }
      break;

    default:
      break;
  }
}

// ---------------- SETUP ----------------

void setup() {
  Serial.begin(115200);
  delay(200);

  for (uint8_t i = 0; i < LED_COUNT; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }

  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);

  Serial.println("IR ANALYZER + LED CONTROLLER");
  Serial.println("--------------------------------");
}

// ---------------- LOOP ----------------

void loop() {

  handleLedModes();

  if (IrReceiver.decode()) {

    auto &data = IrReceiver.decodedIRData;

    Serial.print("Protocol: ");
    Serial.println(getProtocolString(data.protocol));

    Serial.print("ADDR: 0x");
    Serial.println(data.address, HEX);

    Serial.print("CMND: 0x");
    Serial.println(data.command, HEX);

    if (data.protocol == NEC && data.address == 0x00) {

      ledIndex = 0;        // reinicia secuencias
      blinkState = false;
      lastMillis = millis();

      switch (data.command) {

        case 0x16:
          currentMode = ALL_ON;
          Serial.println("Modo: ALL ON");
          break;

        case 0x0C:
          currentMode = BLINK_ALL;
          Serial.println("Modo: BLINK ALL");
          break;

        case 0x18:
          currentMode = SEQ_UP;
          Serial.println("Modo: SECUENCIA ASCENDENTE");
          break;

        case 0x5E:
          currentMode = SEQ_DOWN;
          Serial.println("Modo: SECUENCIA DESCENDENTE");
          break;

        default:
          Serial.println("Comando NEC no asignado");
          break;
      }
    }

    Serial.println("================================");
    IrReceiver.resume();
  }
}
