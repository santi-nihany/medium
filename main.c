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
// Habilitar prints de depuraci?n (0 = off, 1 = on)
#define DEBUG_IR              0
// Habilitar debug TX (prints y toggle LED dentro del callback)
#define DEBUG_TX              0

// Captura cruda tras el primer flanco para depuraci?n
#define RAW_CAPTURE_SAMPLES   200


#define CARRIER_FREQ_HZ       38000     // Frecuencia de la portadora IR
// If DEBUG_TX is enabled, use a slow carrier for visual debugging
#if DEBUG_TX
#define CARRIER_HALF_US       500     // 500 us half-period => 1 kHz carrier (visible)
#else
#define CARRIER_HALF_US       (1000000 / (CARRIER_FREQ_HZ * 2))
#endif
#define IR_TX_PIN GPIO5
// Active level for IR LED driver: set to 0 if driver inverts (common transistor low-side)
#define IR_ACTIVE_LEVEL 1
// Force using GPIO5 for TX (we soldered the shield). Disable PWM carrier.
// If you later move the LED to a PWM-capable pin, set this to 1.
#define USE_PWM_CARRIER 0
#define IR_TX_PWM PWM7

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
   uint8_t level;        // Nivel l?gico (0 o 1)
   uint32_t duration;    // Duraci?n en microsegundos
} IRPulse_t;

static IRPulse_t pulseBuffer[MAX_PULSES];
static uint16_t pulseCount = 0;
static uint8_t lastLevel = 1;      // Estado inicial del pin (reposo = 1)
static uint32_t lastTime = 0;      // Tiempo del ?ltimo flanco
static bool tramaCompleta = false; // Se?al de trama lista para procesar
static bool captureStarted = false; // Flag para ignorar el primer flanco
// (Usaremos muestreo por polling con delayInaccurateUs similar a moduloRF.c)

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

// Busy-wait delay in microseconds using TIMER2 (getTimeUs must be available)
static inline void busyDelayUs(uint32_t us)
{
   uint32_t start = getTimeUs();
   while ((getTimeUs() - start) < us) {
      ;
   }
}

// Forward declaration for carrier callback
void carrierCallback(void* ptr);

// Flag para controlar portadora (declarada antes de uso)
static volatile bool carrierEnabled = false;
static volatile bool carrierPhase = false; // toggled at carrier half-period

// ==== INTERRUPCI?N POR FLANCO ====
void GPIO0_IRQHandler(void) {

    uint32_t fall = Chip_PININT_GetFallStates(LPC_GPIO_PIN_INT) & PININTCH(0);
    uint32_t rise = Chip_PININT_GetRiseStates(LPC_GPIO_PIN_INT) & PININTCH(0);

    if (fall || rise) {

        uint32_t now = getTimeUs();

        // Primer flanco: solo inicializar referencia de tiempo, no guardar pulso
        if (!captureStarted) {
            captureStarted = true;
            lastTime = now;
            // Actualizar nivel segun flanco
            if (fall) {
                lastLevel = 0;
                Chip_PININT_ClearFallStates(LPC_GPIO_PIN_INT, PININTCH(0));
            } else {
                lastLevel = 1;
                Chip_PININT_ClearRiseStates(LPC_GPIO_PIN_INT, PININTCH(0));
            }
            Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(0));
            return;
        }

        uint32_t delta = now - lastTime;

        // Filtrar glitches/rebotes: ignorar pulsos muy cortos
        if (delta < MIN_PULSE_US) {
            // Limpiar flags y salir sin guardar ni actualizar estado
            if (fall) Chip_PININT_ClearFallStates(LPC_GPIO_PIN_INT, PININTCH(0));
            if (rise) Chip_PININT_ClearRiseStates(LPC_GPIO_PIN_INT, PININTCH(0));
            Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(0));
            return;
        }

        lastTime = now;

        // Guardar pulso
        if (pulseCount < MAX_PULSES && delta < 60000) {

            pulseBuffer[pulseCount].duration = delta;

            // Nivel que acaba de terminar
            pulseBuffer[pulseCount].level = lastLevel;

            pulseCount++;
        }

        // Actualizar el nivel actual seg?n flanco
        if (fall) {
            lastLevel = 0;
            Chip_PININT_ClearFallStates(LPC_GPIO_PIN_INT, PININTCH(0));
        }
        else if (rise) {
            lastLevel = 1;
            Chip_PININT_ClearRiseStates(LPC_GPIO_PIN_INT, PININTCH(0));
        }

        Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(0));
    }
}


// ==== CONFIGURAR INTERRUPCI?N EN GPIO ==== check
void enableIrqGPIO(uint8_t irqChannel, uint8_t port, uint8_t pin) {
   Chip_SCU_GPIOIntPinSel(irqChannel, port, pin); // Seleccionar pin
   Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(irqChannel));
   Chip_PININT_SetPinModeEdge(LPC_GPIO_PIN_INT, PININTCH(irqChannel)); // Modo flanco
   Chip_PININT_EnableIntHigh(LPC_GPIO_PIN_INT, PININTCH(irqChannel));
   Chip_PININT_EnableIntLow(LPC_GPIO_PIN_INT, PININTCH(irqChannel));
   NVIC_ClearPendingIRQ(PIN_INT0_IRQn + irqChannel);
   NVIC_EnableIRQ(PIN_INT0_IRQn + irqChannel);
}

//====== DECODIFICADOR NEC =======
bool necDecode(IRPulse_t *pulses, uint16_t count,
               uint8_t *address, uint8_t *command) 
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
   /* Start hardware carrier using TIMER0 at half-period intervals */
   carrierEnabled = true;
   Timer_Init(
      0,
      Timer_microsecondsToTicks(CARRIER_HALF_US),
      carrierCallback
   );
   if (DEBUG_TX) printf("TX DEBUG: carrier ON\r\n");
}

static inline void irCarrierOff(void)
{
   /* Stop hardware carrier (TIMER0) and leave pin in inactive state */
   carrierEnabled = false;
   Timer_DeInit(0);
   gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
   if (DEBUG_TX) printf("TX DEBUG: carrier OFF\r\n");
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
      Timer_microsecondsToTicks(1000), // dummy inicial
      irTxCallback
   );
}
void necSend(uint8_t addr, uint8_t cmd)
{
   txData[0] = addr;
   txData[1] = ~addr;
   txData[2] = cmd;
   txData[3] = ~cmd;

   txActive = true;
   txState = TX_HDR_MARK;

   Timer_SetCompareMatch(
      3,
      TIMERCOMPAREMATCH0,
      Timer_microsecondsToTicks(NEC_HDR_MARK)
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
   //irTxInit();
   // Hardware verification: toggle carrier for 1s using software delays
   // Quick GPIO blink to verify pin control
   void blinkTest(int times) {
      for (int t = 0; t < times; t++) {
         gpioWrite(IR_TX_PIN, IR_ACTIVE_LEVEL);
         delay(200);
         gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
         delay(200);
      }
   }

   // Visible carrier test with adjustable half-period (useful when 38kHz is too fast for delayInaccurateUs)
   void rawCarrierVisibleTest(uint32_t ms, uint32_t halfUs) {
      uint32_t start = getTimeUs();
      uint32_t now = start;
      gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
      printf("TX TEST: visible carrier toggle %lu ms (halfUs=%lu)\r\n", (unsigned long)ms, (unsigned long)halfUs);
      while ((now - start) < ms * 1000) {
         carrierPhase = !carrierPhase;
         gpioWrite(IR_TX_PIN, carrierPhase ? IR_ACTIVE_LEVEL : (1 - IR_ACTIVE_LEVEL));
         delayInaccurateUs(halfUs);
         now = getTimeUs();
      }
      gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
      printf("TX TEST: done\r\n");
   }

   blinkTest(3);
   // visible at 1 kHz (half-period=500us) for 1s
   rawCarrierVisibleTest(1000, 500);
   // Usamos timer de muestreo; el procesamiento se hace en main cuando la trama completa
   int i;
   uint8_t addrToSend, cmdToSend;

   printf("Sistema listo. Esperando se?al IR...\r\n");

   // Usaremos polling con delayInaccurateUs (estilo moduloRF.c)

   while (true) {
      // Use blocking sender (software carrier) for testing ? more reliable on this board
      //necSendBlocking(0xFF,0x00);
      //delay(3000);
      
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
         if (DEBUG_IR) printf("DEBUG: primer flanco detectado level=%d\r\n", cur);
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
                     if (DEBUG_IR) printf("DEBUG: pulso guardado idx=%u level=%d delta=%lu\r\n", (unsigned int)(pulseCount-1), lastLevel, (unsigned long)delta);
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
               if (DEBUG_IR) printf("DEBUG: timeout, tramaCompleta=true pulseCount=%u\r\n", (unsigned int)pulseCount);
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
