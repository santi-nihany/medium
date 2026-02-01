#include <IRremote.hpp>

// ===== CONFIGURACIÓN =====
#define IR_RECEIVE_PIN 2
#define IR_SEND_PIN    3
#define BUTTON_PIN     4

// NEC a emitir (configurable)
#define NEC_ADDRESS 0x20
#define NEC_COMMAND 0x10

// ========================

// Convierte byte a binario tipo 0bxxxxxxxx
void printBinary(uint8_t value) {
  Serial.print("0b");
  for (int i = 7; i >= 0; i--) {
    Serial.print((value >> i) & 1);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
  IrSender.begin(IR_SEND_PIN);

  Serial.println("IR Analyzer + NEC Sender listo");
  Serial.println("--------------------------------");
}

void loop() {

  // ===== RECEPCIÓN IR =====
  if (IrReceiver.decode()) {

    Serial.print("Protocol: ");
    Serial.println(IrReceiver.decodedIRData.protocol);

    Serial.print("Address: 0x");
    Serial.print(IrReceiver.decodedIRData.address, HEX);
    Serial.print("  ");
    printBinary(IrReceiver.decodedIRData.address);
    Serial.println();

    Serial.print("Command: 0x");
    Serial.print(IrReceiver.decodedIRData.command, HEX);
    Serial.print("  ");
    printBinary(IrReceiver.decodedIRData.command);
    Serial.println();

    Serial.print("Raw length: ");
    Serial.println(IrReceiver.decodedIRData.rawDataPtr->rawlen);

    Serial.println("-----");

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
    printBinary(NEC_ADDRESS);
    Serial.println();

    Serial.print("TX Command: 0x");
    Serial.print(NEC_COMMAND, HEX);
    Serial.print(" ");
    printBinary(NEC_COMMAND);
    Serial.println();

    Serial.println("-----");

    delay(300); // debounce simple
  }

  lastButton = button;
}
