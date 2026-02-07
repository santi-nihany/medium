#include "moduloIR.h"
#include "sapi.h"
#include "chip.h"

// ==== CONFIGURACION GENERAL ====
#define IR_INPUT_PIN          GPIO7     // Pin conectado a la salida del KY-022
#define IR_IRQ_CHANNEL        0         // Canal de interrupcion GPIO
#define TIMEOUT_US            40000     // 40 ms sin cambios ? fin de trama
#define MAX_PULSES            200       // M?ximo de pulsos almacenados
#define MIN_PULSE_US          300       // Pulsos menores a esto son ruido/glitch

#define IR_TX_PIN GPIO5

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


// ==== ESTRUCTURA PARA GUARDAR LOS PULSOS ====
typedef struct {
   uint8_t level;        // Nivel logico (0 o 1)
   uint32_t duration;    // Duracion en microsegundos
} IRPulse_t;

static IRPulse_t pulseBuffer[MAX_PULSES];
static uint16_t pulseCount = 0;
static uint8_t lastLevel = 1;      // Estado inicial del pin (reposo = 1)
static uint32_t lastTime = 0;      // Tiempo del ultimo flanco
static bool tramaCompleta = false; // Se?al de trama lista para procesar
static bool captureStarted = false; // Flag para ignorar el primer flanco


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

// ==== INTERRUPCION POR FLANCO ====
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
bool necDecode(IRPulse_t *pulses, uint16_t count,uint8_t *address, uint8_t *command) {
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

        data = (data >> 1) | (bit << 31); // NEC envia LSB primero
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

static inline void irCarrierOn(void){
   gpioWrite(IR_TX_PIN, 1);
}

static inline void irCarrierOff(void){
   gpioWrite(IR_TX_PIN, 0);
}

void irTxCallback(void* ptr){
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
   gpioWrite(IR_TX_PIN, 0);

   Timer_Init(3, Timer_microsecondsToTicks(1000),irTxCallback);
}
void necSend(uint8_t addr, uint8_t cmd)
{
   txData[0] = addr;
   txData[1] = ~addr;
   txData[2] = cmd;
   txData[3] = ~cmd;

   txActive = true;
   txState = TX_HDR_MARK;

   Timer_SetCompareMatch(3,TIMERCOMPAREMATCH0,Timer_microsecondsToTicks(NEC_HDR_MARK));
}
void moduloIR_Init(void){
   gpioConfig(IR_INPUT_PIN, GPIO_INPUT);
   gpioConfig(IR_TX_PIN, GPIO_OUTPUT);
   gpioWrite(IR_TX_PIN, 0);

   timerUsInit();
   irTxInit();

   enableIrqGPIO(IR_IRQ_CHANNEL, 3, 7);

   resetTimeUs();
   lastTime = getTimeUs();

   pulseCount = 0;
   tramaCompleta = false;
   captureStarted = false;
}

bool moduloIR_TramaDisponible(void){
   uint32_t ahora = getTimeUs();

   if (!tramaCompleta && pulseCount > 3 && (ahora - lastTime) > TIMEOUT_US){
      tramaCompleta = true;
   }

   return tramaCompleta;
}
bool moduloIR_GetNEC(uint8_t *addr, uint8_t *cmd){
    bool ok = necDecode(pulseBuffer, pulseCount, addr, cmd);

   /* Reset estado para próxima trama */
   tramaCompleta = false;
   pulseCount = 0;
   lastLevel = 1;
   captureStarted = false;

   resetTimeUs();
   lastTime = getTimeUs();

   return ok;
}
void moduloIR_SendNEC(uint8_t addr, uint8_t cmd){
   necSend(addr, cmd);
}