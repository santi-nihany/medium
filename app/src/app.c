#include <stdint.h>
#include <stdbool.h>
#include "modulo_ir.h"

// ==== PROGRAMA PRINCIPAL ====
int main(void) {
   uartConfig(UART_USB, 115200);

   modulo_ir_init();

   int i;
   uint8_t addrToSend, cmdToSend;
   IRPulse_t localBuf[MAX_PULSES];
   uint16_t count = 0;
   bool captured=false;

   printf("Sistema listo. Esperando señal IR...\r\n");

   while (true) {
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

}