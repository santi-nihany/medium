#include <IRremote.hpp>

#define IR_RECEIVE_PIN 2
#define IR_SEND_PIN    3
#define BUTTON_PIN     4

// NEC a emitir (configurable)
#define NEC_ADDRESS 0xFF
#define NEC_COMMAND 0x00

void printBinary64(uint64_t value, uint8_t bits) {
  Serial.print("0b");
  for (int i = bits - 1; i >= 0; i--) {
    Serial.print(((value >> i) & 1) ? '1' : '0');
  }
}

void printRawBuffer() {
  Serial.println("RAW buffer (mark/space en us):");

  for (uint16_t i = 1; i < IrReceiver.irparams.rawlen; i++) {
    uint32_t duration = IrReceiver.irparams.rawbuf[i] * MICROS_PER_TICK;

    if (i & 1) {
      Serial.print("MARK  ");
    } else {
      Serial.print("SPACE ");
    }

    Serial.println(duration);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
  IrSender.begin(IR_SEND_PIN);


  Serial.println("IR ANALYZER UNIVERSAL (AVR)");
  Serial.println("---------------------------");
}

void loop() {

  if (IrReceiver.decode()) {

    auto &data = IrReceiver.decodedIRData;

    Serial.print("Protocol: ");
    Serial.println(getProtocolString(data.protocol));

    Serial.print("Bits: ");
    Serial.println(data.numberOfBits);

    if (data.protocol != UNKNOWN && data.numberOfBits > 0) {
      Serial.print("Decoded RAW HEX: 0x");
      Serial.println(data.decodedRawData, HEX);

      Serial.print("Decoded RAW BIN: ");
      printBinary64(data.decodedRawData, data.numberOfBits);
      Serial.println();
    } else {
      Serial.println("UNKNOWN protocol -> RAW only");
    }

    Serial.print("Raw length: ");
    Serial.println(IrReceiver.irparams.rawlen);

    printRawBuffer();

    Serial.println("================================");

    IrReceiver.resume();
  }
  // ===== ENVÍO NEC POR BOTÓN =====
  static bool lastButton = HIGH;
  bool button = digitalRead(BUTTON_PIN);

  if (lastButton == HIGH && button == LOW) {
    Serial.println("BOTON PRESIONADO -> Enviando NEC");

    IrSender.sendNEC(NEC_ADDRESS, NEC_COMMAND, 0);

    Serial.print("TX Address: 0x");
    Serial.print(NEC_ADDRESS, HEX);
    Serial.print(" ");
    printBinary64(NEC_ADDRESS,16);
    Serial.println();

    Serial.print("TX Command: 0x");
    Serial.print(NEC_COMMAND, HEX);
    Serial.print(" ");
    printBinary64(NEC_COMMAND,16);
    Serial.println();

    Serial.println("-----");

    delay(300); // debounce simple
  }

  lastButton = button;
}
