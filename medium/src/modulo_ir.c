#include "modulo_ir.h"
#include <stdio.h>

/*==================[internal data]=========================================*/

// Temporizadores y estado interno
static IRPulse_t pulseBuffer[MAX_PULSES];
static uint16_t pulseCount = 0;
static uint8_t lastLevel = 1;
static uint32_t lastTime = 0;
static bool tramaCompleta = false;
static bool captureStarted = false;

// Carrier / TX internals
static volatile bool carrierEnabled = false;
static volatile bool carrierPhase = false;

#define CARRIER_HALF_US       (1000000 / (CARRIER_FREQ_HZ * 2))

// NEC timings utilizados por el emisor
#define NEC_HDR_MARK   9000
#define NEC_HDR_SPACE  4500
#define NEC_BIT_MARK    560
#define NEC_ONE_SPACE  1690
#define NEC_ZERO_SPACE  560
#define NEC_STOP_MARK   560

/*==================[timer functions]=======================================*/

// Aux: timer microsegundos
void timerUsInit(void) {
   Chip_TIMER_Init(LPC_TIMER2);
   Chip_TIMER_PrescaleSet(LPC_TIMER2, (Chip_Clock_GetRate(CLK_MX_TIMER2) / 1000000) - 1);
   Chip_TIMER_Reset(LPC_TIMER2);
   Chip_TIMER_Enable(LPC_TIMER2);
}

uint32_t getTimeUs(void) {
   return Chip_TIMER_ReadCount(LPC_TIMER2);
}

void resetTimeUs(void) {
   Chip_TIMER_Reset(LPC_TIMER2);
}

static inline void busyDelayUs(uint32_t us)
{
   uint32_t start = getTimeUs();
   while ((getTimeUs() - start) < us) {
      ;
   }
}

/*==================[carrier (Timer_0 callback)]============================*/

void carrierCallback(void* ptr)
{
   if (carrierEnabled) {
      carrierPhase = !carrierPhase;
      gpioWrite(IR_TX_PIN, carrierPhase ? IR_ACTIVE_LEVEL : (1 - IR_ACTIVE_LEVEL));
   } else {
      gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
   }
}

void irCarrierOn(void)
{
   carrierEnabled = true;
   Timer_Init(
      0,
      Timer_microsecondsToTicks(CARRIER_HALF_US),
      carrierCallback
   );
}

void irCarrierOff(void)
{
   carrierEnabled = false;
   Timer_DeInit(0);
   gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
}

void irTxInit(void)
{
   gpioConfig(IR_TX_PIN, GPIO_OUTPUT);
   gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
}

/*==================[NEC transmitter (blocking)]============================*/

// Emisor NEC bloqueante
void modulo_ir_send_nec(uint8_t addr, uint8_t cmd)
{
   uint8_t addrInv = ~addr;
   uint8_t cmdInv = ~cmd;
   uint32_t data = (uint32_t)addr | ((uint32_t)addrInv << 8) | ((uint32_t)cmd << 16) | ((uint32_t)cmdInv << 24);

   gpioConfig(IR_TX_PIN, GPIO_OUTPUT);
   gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);

   uint32_t start;

   // Header MARK (carrier on)
   start = getTimeUs();
   __disable_irq();
   while ((getTimeUs() - start) < NEC_HDR_MARK) {
      carrierPhase = !carrierPhase;
      gpioWrite(IR_TX_PIN, carrierPhase ? IR_ACTIVE_LEVEL : (1 - IR_ACTIVE_LEVEL));
      busyDelayUs(CARRIER_HALF_US);
   }
   // Header SPACE
   gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
   busyDelayUs(NEC_HDR_SPACE);

   // 32 bits LSB first
   for (int i = 0; i < 32; i++) {
      // MARK
      start = getTimeUs();
      while ((getTimeUs() - start) < NEC_BIT_MARK) {
         carrierPhase = !carrierPhase;
         gpioWrite(IR_TX_PIN, carrierPhase ? IR_ACTIVE_LEVEL : (1 - IR_ACTIVE_LEVEL));
         busyDelayUs(CARRIER_HALF_US);
      }

      // SPACE
      uint8_t bit = (data >> i) & 1;
      gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
      busyDelayUs(bit ? NEC_ONE_SPACE : NEC_ZERO_SPACE);
   }

   // STOP MARK
   start = getTimeUs();
   while ((getTimeUs() - start) < NEC_STOP_MARK) {
      carrierPhase = !carrierPhase;
      gpioWrite(IR_TX_PIN, carrierPhase ? IR_ACTIVE_LEVEL : (1 - IR_ACTIVE_LEVEL));
      busyDelayUs(CARRIER_HALF_US);
   }

   gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
   __enable_irq();
}

/*==================[NEC decoder]===========================================*/

// Decodificador NEC
bool modulo_ir_decode(IRPulse_t *pulses, uint16_t count, uint8_t *address, uint8_t *command)
{
    if (count < 4) return false;
    if (!(pulses[0].level == 0 && pulses[0].duration > 8500 && pulses[0].duration < 9500)) return false;
    if (!(pulses[1].level == 1 && pulses[1].duration > 4000 && pulses[1].duration < 5000)) return false;

    uint32_t data = 0;
    for (int i = 2; i+1 < count; i += 2) {
        if (!(pulses[i].level == 0 && pulses[i].duration > 400 && pulses[i].duration < 700)) return false;
        uint32_t high = pulses[i+1].duration;
        uint8_t bit;
        if (high > 400 && high < 700) bit = 0;
        else if (high > 1500 && high < 2000) bit = 1;
        else return false;
        data = (data >> 1) | (bit << 31);
    }

    uint8_t addr     =  data        & 0xFF;
    uint8_t addrInv  = (data >> 8)  & 0xFF;
    uint8_t cmd      = (data >> 16) & 0xFF;
    uint8_t cmdInv   = (data >> 24) & 0xFF;

    if (addr != (uint8_t)~addrInv) return false;
    if (cmd  != (uint8_t)~cmdInv)  return false;

    *address = addr;
    *command = cmd;
    return true;
}

/*==================[initialization]========================================*/

// Inicializacion publica
void modulo_ir_init(void)
{
   timerUsInit();
   irTxInit();
   gpioConfig(IR_INPUT_PIN, GPIO_INPUT);
}

/*==================[polling capture]=======================================*/

// Captura una trama. Escribe hasta MAX_PULSES en outBuffer.
bool modulo_ir_capture(IRPulse_t *outBuffer, uint16_t *outCount)
{
   // Esperar primer flanco
   uint32_t waitTimeout = 0;
   uint8_t cur = gpioRead(IR_INPUT_PIN);
   while (cur == lastLevel) {
      delayInaccurateUs(SAMPLE_US);
      waitTimeout += SAMPLE_US;
      if (waitTimeout > 3000000) break;
      cur = gpioRead(IR_INPUT_PIN);
   }

   if (cur != lastLevel) {
      captureStarted = true;
      lastLevel = cur;
      pulseCount = 0;
      lastTime = getTimeUs();

      while (true) {
         uint8_t level = gpioRead(IR_INPUT_PIN);
         uint32_t now = getTimeUs();

         if (level != lastLevel) {
            uint32_t delta = now - lastTime;
            if (delta >= MIN_PULSE_US) {
               if (pulseCount < MAX_PULSES && delta < 60000) {
                  pulseBuffer[pulseCount].duration = delta;
                  pulseBuffer[pulseCount].level = lastLevel;
                  pulseCount++;
               }
               lastTime = now;
            } else {
               lastTime = now;
            }
            lastLevel = level;
         }

         if (pulseCount > 3 && (now - lastTime) > TIMEOUT_US) {
            tramaCompleta = true;
         }

         if (tramaCompleta) break;

         delayInaccurateUs(SAMPLE_US);
      }

      if (tramaCompleta) {
         // Copiar al buffer de salida
         uint16_t toCopy = pulseCount < MAX_PULSES ? pulseCount : MAX_PULSES;
         memcpy(outBuffer, pulseBuffer, sizeof(IRPulse_t) * toCopy);
         *outCount = toCopy;

         // Reset estado
         tramaCompleta = false;
         pulseCount = 0;
         lastLevel = 1;
         captureStarted = false;
         return true;
      }
   }

   return false;
}
