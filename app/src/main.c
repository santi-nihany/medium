//===----------------------------------------------------------------------===//
///
/// \file
/// Inicio del programa. Contiene llamados a inicialización y bucle principal.
///
//===----------------------------------------------------------------------===//

#include "main.h"
#include "display.h"
#include "sprites.h"
#include "modulo_ir.h"

int main(void) {
  // Inicializar placa, puertos y protocolos
  boardConfig();
  i2cInit(I2C0, 100000);

  // Inicializar componentes
  displayInit();
   
   
   modulo_ir_init();

  int i;
  uint8_t addrToSend, cmdToSend;
  IRPulse_t localBuf[MAX_PULSES];
  uint16_t count = 0;
  bool captured=false;

  displayPlace(Sprite_0001, 0, 0);
  displayUpdate();

  bool_t tec1, tec2;
  while (1) {
    if (tec1 != !gpioRead(TEC1)) {
      tec1 = !gpioRead(TEC1);
      gpioWrite(LED1, tec1);
      if (tec1) {
        displayDrawRectangle(64, 32, Sprite_0002.width, Sprite_0002.height,
                             DISPLAY_BLACK, true);
        displayDrawRectangle(104, 32, Sprite_0003.width, Sprite_0003.height,
                             DISPLAY_BLACK, true);
        displayPlace(Sprite_0002, 19, 32);
        displayPlace(Sprite_0003, 59, 32);
        displayUpdate();
      }
    }
    if (tec2 != !gpioRead(TEC2)) {
      tec2 = !gpioRead(TEC2);
      gpioWrite(LED2, tec2);
      if (tec2) {
        displayDrawRectangle(19, 32, Sprite_0002.width, Sprite_0002.height,
                             DISPLAY_BLACK, true);
        displayDrawRectangle(59, 32, Sprite_0003.width, Sprite_0003.height,
                             DISPLAY_BLACK, true);
        displayPlace(Sprite_0002, 64, 32);
        displayPlace(Sprite_0003, 104, 32);
        displayUpdate();
      }
    }
    captured = modulo_ir_capture(localBuf, &count);
      if (captured) {
         printf("\n\nTrama capturada (%d pulsos):\r\n", count);
         for (i = 0; i < count; i++) {
            printf("[%03d] Nivel=%d  Dur=%lu us\r\n", i, localBuf[i].level, (unsigned long)localBuf[i].duration);
         }
         uint8_t addr, cmd;
         bool ok = modulo_ir_decode(localBuf, count, &addr, &cmd);
         if (ok) {
            addrToSend = addr;
            cmdToSend = cmd;
            printf("NEC decodificado OK! Addr=0x%02X  Cmd=0x%02X\r\n", addr, cmd);
            printf("NEC to send Addr=0x%02X  Cmd=0x%02X\r\n", addrToSend, cmdToSend);
            modulo_ir_send_nec(addrToSend, cmdToSend);
         } else {
            printf("ERROR: Trama no valida NEC\r\n");
         }
      }
  }

  return 0;
}