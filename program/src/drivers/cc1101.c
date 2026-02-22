//===----------------------------------------------------------------------===//
///
/// \file
/// Driver SPI para el transceptor CC1101.
///
//===----------------------------------------------------------------------===//

#include "drivers/cc1101.h"
#include "modules/spi_bus.h"

/// Timeout de espera de SO/MISO cuando el chip está listo
#define CC1101_READY_TIMEOUT 100000U
/// Frecuencia del cristal del CC1101 en Hz
#define CC1101_XOSC_HZ 26000000UL
/// Escala de frecuencia para registros FREQx
#define CC1101_FREQ_SCALE (1UL << 16)
/// Timeout para esperar estado MARCSTATE estable
#define CC1101_STATE_TIMEOUT_US 10000UL

/// Par interno dirección/valor para configurar registros.
typedef struct {
  uint8_t address;
  uint8_t value;
} cc1101RegisterSetting_t;

/// PATABLE recomendada para OOK 315MHz.
const uint8_t CC1101_OOK_PA_TABLE_315[8] = {0x00, 0xC0, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00};

/// PATABLE recomendada para OOK 433MHz.
const uint8_t CC1101_OOK_PA_TABLE_433[8] = {0x00, 0xC0, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00};

const cc1101OokConfig_t CC1101_OOK_CONFIG_433 = {
    .band = CC1101_BAND_433MHZ,
    .frequencyHz = 433920000UL,
    .preset = CC1101_OOK_PRESET_AM650_ASYNC,
    .paTable = CC1101_OOK_PA_TABLE_433,
    .paTableSize = 8,
};

const cc1101OokConfig_t CC1101_OOK_CONFIG_315 = {
    .band = CC1101_BAND_315MHZ,
    .frequencyHz = 315000000UL,
    .preset = CC1101_OOK_PRESET_AM650_ASYNC,
    .paTable = CC1101_OOK_PA_TABLE_315,
    .paTableSize = 8,
};

/// Preset OOK async, BW 270kHz.
static const cc1101RegisterSetting_t cc1101PresetOok270[] = {
    {CC1101_IOCFG0, 0x0D},   {CC1101_FIFOTHR, 0x47},  {CC1101_PKTCTRL0, 0x32},
    {CC1101_FSCTRL1, 0x06},  {CC1101_MDMCFG0, 0x00},  {CC1101_MDMCFG1, 0x00},
    {CC1101_MDMCFG2, 0x30},  {CC1101_MDMCFG3, 0x32},  {CC1101_MDMCFG4, 0x67},
    {CC1101_MCSM0, 0x18},    {CC1101_FOCCFG, 0x18},   {CC1101_AGCCTRL0, 0x40},
    {CC1101_AGCCTRL1, 0x00}, {CC1101_AGCCTRL2, 0x03}, {CC1101_WORCTRL, 0xFB},
    {CC1101_FREND0, 0x11},   {CC1101_FREND1, 0xB6},   {0x00, 0x00},
};

/// Preset OOK async, BW 650kHz.
static const cc1101RegisterSetting_t cc1101PresetOok650[] = {
    {CC1101_IOCFG0, 0x0D},   {CC1101_FIFOTHR, 0x07},  {CC1101_PKTCTRL0, 0x32},
    {CC1101_FSCTRL1, 0x06},  {CC1101_MDMCFG0, 0x00},  {CC1101_MDMCFG1, 0x00},
    {CC1101_MDMCFG2, 0x30},  {CC1101_MDMCFG3, 0x32},  {CC1101_MDMCFG4, 0x17},
    {CC1101_MCSM0, 0x18},    {CC1101_FOCCFG, 0x18},   {CC1101_AGCCTRL0, 0x91},
    {CC1101_AGCCTRL1, 0x00}, {CC1101_AGCCTRL2, 0x07}, {CC1101_WORCTRL, 0xFB},
    {CC1101_FREND0, 0x11},   {CC1101_FREND1, 0xB6},   {0x00, 0x00},
};

static uint32_t cc1101CurrentFrequencyHz = 433920000UL;

/// Baja CS para iniciar una transacción SPI con el CC1101.
static void cc1101_select(void) { gpioWrite(CC1101_CS_PIN, FALSE); }

/// Sube CS para finalizar una transacción SPI con el CC1101.
static void cc1101_deselect(void) { gpioWrite(CC1101_CS_PIN, TRUE); }

/// Espera hasta que SO/MISO indique que el CC1101 está listo para SPI.
static bool_t cc1101_waitReady(void) {
  uint32_t timeout = CC1101_READY_TIMEOUT;
  while (timeout--) {
    if (!gpioRead(SPI_MISO)) {
      return TRUE;
    }
  }
  return FALSE;
}

/// Inicia una transacción SPI y verifica estado listo del CC1101.
static bool_t cc1101_beginTransaction(void) {
  if (!spiBusLock(portMAX_DELAY)) {
    return FALSE;
  }

  cc1101_select();
  if (!cc1101_waitReady()) {
    cc1101_deselect();
    spiBusUnlock();
    return FALSE;
  }
  return TRUE;
}

/// Cierra una transacción SPI activa.
static void cc1101_endTransaction(void) {
  cc1101_deselect();
  spiBusUnlock();
}

/// Escribe una lista de registros hasta encontrar el terminador (0x00, 0x00).
static bool_t
cc1101_applyRegisterConfig(const cc1101RegisterSetting_t *config) {
  uint16_t i = 0;

  if (config == NULL) {
    return FALSE;
  }

  while (!(config[i].address == 0x00 && config[i].value == 0x00)) {
    if (!cc1101_writeRegister(config[i].address, config[i].value)) {
      return FALSE;
    }
    i++;
  }

  return TRUE;
}

/// Convierte frecuencia absoluta a palabra FREQ (24 bits).
static uint32_t cc1101_frequencyToWord(uint32_t frequencyHz) {
  uint64_t freqWord =
      ((uint64_t)frequencyHz * CC1101_FREQ_SCALE) / CC1101_XOSC_HZ;
  return (uint32_t)(freqWord & 0xFFFFFFUL);
}

/// Convierte palabra FREQ (24 bits) a frecuencia absoluta.
static uint32_t cc1101_wordToFrequency(uint32_t freqWord) {
  uint64_t frequency =
      ((uint64_t)freqWord * CC1101_XOSC_HZ) / CC1101_FREQ_SCALE;
  return (uint32_t)frequency;
}

bool_t cc1101_init(void) {
  gpioInit(CC1101_CS_PIN, GPIO_OUTPUT);
  gpioInit(CC1101_GDO0_PIN, GPIO_INPUT);
  gpioInit(CC1101_GDO2_PIN, GPIO_INPUT);
  cc1101_deselect();
  delayInaccurateUs(1000);

  if (!cc1101_reset()) {
#ifdef MEDIUM_DEBUG
    printf("[drivers] [cc1101] ERROR: reset fallo\r\n");
#endif
    return FALSE;
  }

  if (!cc1101_applyOokConfig(&CC1101_OOK_CONFIG_433)) {
#ifdef MEDIUM_DEBUG
    printf("[drivers] [cc1101] ERROR: config base OOK 433 fallo\r\n");
#endif
    return FALSE;
  }

  if (!cc1101_flushRxFifo() || !cc1101_flushTxFifo() || !cc1101_enterIdle()) {
#ifdef MEDIUM_DEBUG
    printf("[drivers] [cc1101] ERROR: flush/idle fallo\r\n");
#endif
    return FALSE;
  }

#ifdef MEDIUM_DEBUG
  printf("[drivers] [cc1101] Init OK PART=0x%02X VER=0x%02X\r\n",
         cc1101_readRegister(CC1101_PARTNUM),
         cc1101_readRegister(CC1101_VERSION));
#endif

  return TRUE;
}

bool_t cc1101_reset(void) {
  uint8_t command = CC1101_SRES;

  cc1101_deselect();
  delayInaccurateUs(1000);

  cc1101_select();
  delayInaccurateUs(1000);
  cc1101_deselect();
  delayInaccurateUs(1000);

  if (!cc1101_beginTransaction()) {
#ifdef MEDIUM_DEBUG
    printf("[drivers] [cc1101] ERROR: beginTransaction en reset\r\n");
#endif
    return FALSE;
  }

  spiWrite(SPI0, &command, 1);

  if (!cc1101_waitReady()) {
#ifdef MEDIUM_DEBUG
    printf("[drivers] [cc1101] ERROR: waitReady post-SRES timeout\r\n");
#endif
    cc1101_endTransaction();
    return FALSE;
  }

  cc1101_endTransaction();
  delayInaccurateUs(1000);
  return TRUE;
}

bool_t cc1101_calibrate(void) {
  if (!cc1101_commandStrobe(CC1101_SCAL)) {
    return FALSE;
  }
  return cc1101_waitState(CC1101_STATE_IDLE, CC1101_STATE_TIMEOUT_US);
}

bool_t cc1101_waitState(cc1101MarcState_t state, uint32_t timeoutUs) {
  uint32_t startCycles = cyclesCounterRead();
  while (
      ((uint32_t)(((uint64_t)(cyclesCounterRead() - startCycles) * 1000000ULL) /
                  (uint64_t)SystemCoreClock)) < timeoutUs) {
    uint8_t status = cc1101_readRegister(CC1101_MARCSTATE) & 0x1F;
    if (status == (uint8_t)state) {
      return TRUE;
    }
    delayInaccurateUs(30);
  }
  return FALSE;
}

bool_t cc1101_applyModPreset(cc1101ModPreset_t preset) {
  if (!cc1101_enterIdle()) {
    return FALSE;
  }

  switch (preset) {
  case CC1101_OOK_PRESET_AM270_ASYNC:
    return cc1101_applyRegisterConfig(cc1101PresetOok270);
  case CC1101_OOK_PRESET_AM650_ASYNC:
    return cc1101_applyRegisterConfig(cc1101PresetOok650);
  default:
    return FALSE;
  }
}

bool_t cc1101_applyOokConfig(const cc1101OokConfig_t *config) {
  if (config == NULL || config->frequencyHz == 0U || config->paTable == NULL ||
      config->paTableSize == 0U) {
    return FALSE;
  }

  if (!cc1101_applyModPreset(config->preset)) {
    return FALSE;
  }

  if (!cc1101_writePaTable(config->paTable, config->paTableSize)) {
    return FALSE;
  }

  if (!cc1101_setFrequency(config->frequencyHz)) {
    return FALSE;
  }

  if (!cc1101_calibrate()) {
    return FALSE;
  }

  if (!cc1101_flushRxFifo() || !cc1101_flushTxFifo()) {
    return FALSE;
  }

#ifdef MEDIUM_DEBUG
  {
    const char *presetName = "AM650";
    if (config->preset == CC1101_OOK_PRESET_AM270_ASYNC) {
      presetName = "AM270";
    }
    printf("[drivers] [cc1101] RF cfg OK f=%luHz preset=%s band=%s\r\n",
           (unsigned long)config->frequencyHz, presetName,
           (config->band == CC1101_BAND_315MHZ) ? "315" : "433");
  }
#endif

  return TRUE;
}

bool_t cc1101_setFrequency(uint32_t frequencyHz) {
  uint32_t freqWord = cc1101_frequencyToWord(frequencyHz);

  if (freqWord > 0xFFFFFFUL) {
    return FALSE;
  }

  if (!cc1101_writeRegister(CC1101_FREQ2, (uint8_t)(freqWord >> 16)) ||
      !cc1101_writeRegister(CC1101_FREQ1, (uint8_t)(freqWord >> 8)) ||
      !cc1101_writeRegister(CC1101_FREQ0, (uint8_t)(freqWord))) {
    return FALSE;
  }

  cc1101CurrentFrequencyHz = cc1101_wordToFrequency(freqWord);
  return TRUE;
}

uint32_t cc1101_getFrequency(void) { return cc1101CurrentFrequencyHz; }

bool_t cc1101_writeRegister(uint8_t address, uint8_t value) {
  uint8_t buffer[2] = {address, value};

  if (!cc1101_beginTransaction()) {
    return FALSE;
  }

  spiWrite(SPI0, buffer, sizeof(buffer));
  cc1101_endTransaction();
  return TRUE;
}

uint8_t cc1101_readRegister(uint8_t address) {
  uint8_t command = address | CC1101_READ_SINGLE;
  uint8_t data = 0;

  if (address >= CC1101_PARTNUM) {
    command |= CC1101_BURST;
  }

  if (!cc1101_beginTransaction()) {
    return 0;
  }

  spiWrite(SPI0, &command, 1);
  spiRead(SPI0, &data, 1);
  cc1101_endTransaction();
  return data;
}

bool_t cc1101_writeBurstRegister(uint8_t address, const uint8_t *data,
                                 uint8_t size) {
  uint8_t command = address | CC1101_BURST;

  if (data == NULL || size == 0) {
    return FALSE;
  }

  if (!cc1101_beginTransaction()) {
    return FALSE;
  }

  spiWrite(SPI0, &command, 1);
  spiWrite(SPI0, (uint8_t *)data, size);
  cc1101_endTransaction();
  return TRUE;
}

bool_t cc1101_readBurstRegister(uint8_t address, uint8_t *data, uint8_t size) {
  uint8_t command = address | CC1101_READ_SINGLE | CC1101_BURST;

  if (data == NULL || size == 0) {
    return FALSE;
  }

  if (!cc1101_beginTransaction()) {
    return FALSE;
  }

  spiWrite(SPI0, &command, 1);
  spiRead(SPI0, data, size);
  cc1101_endTransaction();
  return TRUE;
}

bool_t cc1101_commandStrobe(uint8_t strobe) {
  if (!cc1101_beginTransaction()) {
    return FALSE;
  }

  spiWrite(SPI0, &strobe, 1);
  cc1101_endTransaction();
  return TRUE;
}

bool_t cc1101_enterRx(void) { return cc1101_commandStrobe(CC1101_SRX); }

bool_t cc1101_enterTx(void) { return cc1101_commandStrobe(CC1101_STX); }

bool_t cc1101_enterIdle(void) { return cc1101_commandStrobe(CC1101_SIDLE); }

bool_t cc1101_flushRxFifo(void) {
  if (!cc1101_enterIdle()) {
    return FALSE;
  }
  return cc1101_commandStrobe(CC1101_SFRX);
}

bool_t cc1101_flushTxFifo(void) {
  if (!cc1101_enterIdle()) {
    return FALSE;
  }
  return cc1101_commandStrobe(CC1101_SFTX);
}

bool_t cc1101_setChannel(uint8_t channel) {
  return cc1101_writeRegister(CC1101_CHANNR, channel);
}

bool_t cc1101_setDeviceAddress(uint8_t address) {
  return cc1101_writeRegister(CC1101_ADDR, address);
}

bool_t cc1101_writePaTable(const uint8_t *paTable, uint8_t size) {
  return cc1101_writeBurstRegister(CC1101_PATABLE, paTable, size);
}

bool_t cc1101_writeTxFifo(const uint8_t *data, uint8_t size) {
  return cc1101_writeBurstRegister(CC1101_FIFO, data, size);
}

bool_t cc1101_readRxFifo(uint8_t *data, uint8_t size) {
  return cc1101_readBurstRegister(CC1101_FIFO, data, size);
}

uint8_t cc1101_rxBytes(void) {
  return cc1101_readRegister(CC1101_RXBYTES) & 0x7F;
}

uint8_t cc1101_txBytes(void) {
  return cc1101_readRegister(CC1101_TXBYTES) & 0x7F;
}

bool_t cc1101_gdo0Read(void) { return gpioRead(CC1101_GDO0_PIN); }

bool_t cc1101_gdo2Read(void) { return gpioRead(CC1101_GDO2_PIN); }
