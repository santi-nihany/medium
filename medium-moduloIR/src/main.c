#include "main.h"
#include "moduloIR.h"

int main(void)
{
   boardConfig();
   uartConfig(UART_USB, 115200);

   printf("Sistema IR iniciado\r\n");

   moduloIR_Init();

   uint8_t addr, cmd;

   while (true) {

      if (moduloIR_TramaDisponible()) {

         if (moduloIR_GetNEC(&addr, &cmd)) {
            printf("NEC OK Addr=0x%02X Cmd=0x%02X\r\n", addr, cmd);
            moduloIR_SendNEC(addr, cmd);
         } else {
            printf("Trama IR invalida\r\n");
         }
      }
   }
}