#include "sapi.h"
#include "chip.h"
#include "sapi_timer.h"
#include <stdint.h>
#include <stdbool.h>


// ==== CONFIGURACI?N GENERAL ====
#define IR_INPUT_PIN          GPIO7     // Pin conectado a la salida del KY-022
#define IR_IRQ_CHANNEL        0         // Canal de interrupci?n GPIO
#define TIMEOUT_US            40000     // 40 ms sin cambios ? fin de trama
#define MAX_PULSES            200       // M?ximo de pulsos almacenados
#define MIN_PULSE_US          300       // Pulsos menores a esto son ruido/glitch

#define SAMPLE_US             30        // Intervalo de muestreo en us para polling (menos = mejor resoluci?n)

#define CARRIER_FREQ_HZ       38000     // Frecuencia de la portadora IR

#define IR_TX_PIN GPIO5
// Nivel activo para el controlador LED IR
#define IR_ACTIVE_LEVEL 1

#define NEC_HDR_MARK   9000
#define NEC_HDR_SPACE  4500
#define NEC_BIT_MARK    560
#define NEC_ONE_SPACE  1690
#define NEC_ZERO_SPACE  560
#define NEC_STOP_MARK   560

typedef enum {
   TX_IDLE,
   TX_HDR_MARK,
   TX_HDR_SPACE,
   TX_BIT_MARK,
   TX_BIT_SPACE,
   TX_STOP
} txState_t;

volatile txState_t txState = TX_IDLE;

volatile uint8_t txData[4];
volatile uint8_t txByte = 0;
volatile uint8_t txBit = 0;

volatile bool txActive = false;
static volatile uint32_t txNextChangeUs = 0;
static volatile uint32_t lastCarrierToggleUs = 0;


// ==== ESTRUCTURA PARA GUARDAR LOS PULSOS ====
typedef struct {
   uint8_t level;        // Nivel logico (0 o 1)
   uint32_t duration;    // Duracion en microsegundos
} IRPulse_t;

static IRPulse_t pulseBuffer[MAX_PULSES];
static uint16_t pulseCount = 0;
static uint8_t lastLevel = 1;      // Estado inicial del pin (reposo = 1)
static uint32_t lastTime = 0;      // Tiempo del ultimo flanco
static bool tramaCompleta = false; // Seial de trama lista para procesar
static bool captureStarted = false; // Flag para ignorar el primer flanco

// Flag para controlar portadora (declarada antes de uso)
static volatile bool carrierEnabled = false;
static volatile bool carrierPhase = false; // toggled at carrier half-period

// ==== TIMER para medir tiempo en microsegundos ====
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

// Busy-wait delay en microsegundos usando TIMER2 
static inline void busyDelayUs(uint32_t us)
{
   uint32_t start = getTimeUs();
   while ((getTimeUs() - start) < us) {
      ;
   }
}

// Forward declaration for carrier callback
void carrierCallback(void* ptr);        //TODO: ver si lo puedo volar

//====== DECODIFICADOR NEC =======
bool necDecode(IRPulse_t *pulses, uint16_t count,uint8_t *address, uint8_t *command) 
{
    // --- Validar encabezado NEC ---
    // Pulso 0 = LOW 9ms
    if (!(pulses[0].level == 0 &&
         pulses[0].duration > 8500 &&
         pulses[0].duration < 9500)) {
        return false;
    }

    // Pulso 1 = HIGH 4.5ms
    if (!(pulses[1].level == 1 &&
         pulses[1].duration > 4000 &&
         pulses[1].duration < 5000)) {
        return false;
    }

    uint32_t data = 0;

    // Los bits NEC empiezan en pulso 2:
    // Cada bit: LOW (560us) + HIGH (560us o 1690us)
    // Pulso 2 = LOW 560
    for (int i = 2; i+1 < count; i += 2) {

        // Pulso LOW corto (start bit)
        if (!(pulses[i].level == 0 &&
             pulses[i].duration > 400 &&
             pulses[i].duration < 700)) 
        {
            return false; // fallo en bit LOW
        }

        // Duraci?n HIGH = define bit
        uint32_t high = pulses[i+1].duration;

        uint8_t bit;

        if (high > 400 && high < 700) {
            bit = 0; // bit corto
        }
        else if (high > 1500 && high < 2000) {
            bit = 1; // bit largo
        }
        else {
            return false; // no coincide
        }

        data = (data >> 1) | (bit << 31); // NEC env?a LSB primero
    }

    uint8_t addr     =  data        & 0xFF;
    uint8_t addrInv  = (data >> 8)  & 0xFF;
    uint8_t cmd      = (data >> 16) & 0xFF;
    uint8_t cmdInv   = (data >> 24) & 0xFF;

    // Verificaci?n NEC obligatoria
    if (addr != (uint8_t)~addrInv) return false;
    if (cmd  != (uint8_t)~cmdInv)  return false;

    *address = addr;
    *command = cmd;

    return true;
}

void carrierCallback(void* ptr)
{
   if (carrierEnabled) {
      carrierPhase = !carrierPhase;
      gpioWrite(IR_TX_PIN, carrierPhase ? IR_ACTIVE_LEVEL : (1 - IR_ACTIVE_LEVEL));
   } else {
      gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
   }
}

static inline void irCarrierOn(void)
{
   /* Iniciar el portador de hardware usando TIMER0 a intervalos de medio período */
   carrierEnabled = true;
   Timer_Init(
      0,
      Timer_microsecondsToTicks(CARRIER_HALF_US),
      carrierCallback
   );
}

static inline void irCarrierOff(void)
{
   /* Detener el portador de hardware (TIMER0) y dejar el pin en estado inactivo */
   carrierEnabled = false;
   Timer_DeInit(0);
   gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
}

void irTxCallback(void* ptr)
{
   uint32_t nextTimeUs = 0;

   switch (txState) {

   case TX_HDR_MARK:
      irCarrierOn();
      nextTimeUs = NEC_HDR_MARK;
      txState = TX_HDR_SPACE;
      break;

   case TX_HDR_SPACE:
      irCarrierOff();
      nextTimeUs = NEC_HDR_SPACE;
      txState = TX_BIT_MARK;
      txByte = 0;
      txBit = 0;
      break;

   case TX_BIT_MARK:
      irCarrierOn();
      nextTimeUs = NEC_BIT_MARK;
      txState = TX_BIT_SPACE;
      break;

   case TX_BIT_SPACE: {
      irCarrierOff();
      uint8_t bit = (txData[txByte] >> txBit) & 1;
      nextTimeUs = bit ? NEC_ONE_SPACE : NEC_ZERO_SPACE;

      txBit++;
      if (txBit == 8) {
         txBit = 0;
         txByte++;
         if (txByte == 4) {
            txState = TX_STOP;
            break;
         }
      }
      txState = TX_BIT_MARK;
      break;
   }

   case TX_STOP:
      irCarrierOn();
      nextTimeUs = NEC_STOP_MARK;
      txState = TX_IDLE;
      txActive = false;
      break;

   case TX_IDLE:
   default:
      irCarrierOff();
      return;
   }

   Timer_SetCompareMatch(
      3,
      TIMERCOMPAREMATCH0,
      Timer_microsecondsToTicks(nextTimeUs)
   );
}

void irTxInit(void)
{
   gpioConfig(IR_TX_PIN, GPIO_OUTPUT);
   gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);

   Timer_Init(
      3,
      Timer_microsecondsToTicks(1000), 
      irTxCallback
   );
}
// Blocking, software-driven NEC sender using delayInaccurateUs for carrier toggling
void necSendBlocking(uint8_t addr, uint8_t cmd)
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


// ==== PROGRAMA PRINCIPAL ====
int main(void) {
   boardConfig();
   uartConfig(UART_USB, 115200);
   
   gpioConfig(IR_INPUT_PIN, GPIO_INPUT);
   gpioConfig(IR_TX_PIN, GPIO_OUTPUT);
   gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
   timerUsInit();

   int i;
   uint8_t addrToSend, cmdToSend;

   printf("Sistema listo. Esperando se?al IR...\r\n");

   while (true) {
      
      // Esperar primer flanco (cambio respecto a nivel de reposo)
      uint32_t waitTimeout = 0;
      uint8_t cur = gpioRead(IR_INPUT_PIN);
      // esperar cambio de estado
      while (cur == lastLevel) {
         delayInaccurateUs(SAMPLE_US);
         waitTimeout += SAMPLE_US;
         if (waitTimeout > 3000000) break; // 3 s timeout
         cur = gpioRead(IR_INPUT_PIN);
      }

      if (cur != lastLevel) {
         // primer flanco detectado
         captureStarted = true;
         lastLevel = cur;
         pulseCount = 0;

         // Capturar hasta timeout de INACTIVIDAD usando delayInaccurateUs
         lastTime = getTimeUs(); // referencia de tiempo del primer flanco
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
                  // glitch, actualizar referencia
                  lastTime = now;
               }
               lastLevel = level;
            }

            // Detectar fin de trama por inactividad
            if (pulseCount > 3 && (now - lastTime) > TIMEOUT_US) {
               tramaCompleta = true;
            }

            if (tramaCompleta) {
               break;
            }

            delayInaccurateUs(SAMPLE_US);
         }

         if (tramaCompleta) {
            printf("\n\nTrama capturada (%d pulsos):\r\n", pulseCount);
            for (i = 0; i < pulseCount; i++) {
               printf("[%03d] Nivel=%d  Dur=%lu us\r\n", i, pulseBuffer[i].level, (unsigned long)pulseBuffer[i].duration);
            }
            uint8_t addr, cmd;
            bool ok = necDecode(pulseBuffer, pulseCount, &addr, &cmd);

            // reset estado
            tramaCompleta = false;
            pulseCount = 0;
            lastLevel = 1;
            captureStarted = false;

            if (ok) {
               addrToSend=addr;
               cmdToSend=cmd;
               printf("NEC decodificado OK! Addr=0x%02X  Cmd=0x%02X\r\n", addr, cmd);
               printf("NEC to send Addr=0x%02X  Cmd=0x%02X\r\n", addrToSend, cmdToSend);

               necSendBlocking(addrToSend, cmdToSend);
            } else {
               printf("ERROR: Trama no valida NEC\r\n");
            }
         }
      }
   }


}
