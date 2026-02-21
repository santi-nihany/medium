//===----------------------------------------------------------------------===//
///
/// \file
/// Librería para utilizar un módulo de radiofrecuencia CC1101 por SPI.
///
//===----------------------------------------------------------------------===//

#include "drivers/cc1101.h"
#include "modules/spi_bus.h"

/// Timeout de espera de SO/MISO cuando el chip está listo
#define CC1101_READY_TIMEOUT 100000U
/// Frecuencia del cristal del CC1101 en Hz
#define CC1101_XOSC_HZ 26000000UL
/// Sync mode por defecto: 30/32 bits
#define CC1101_SYNC_MODE_30_32 0x03

/// Par interno dirección/valor para configurar registros.
typedef struct {
  uint8_t address;
  uint8_t value;
} cc1101RegisterSetting_t;

/// PATABLE recomendada para OOK en 433 MHz.
const uint8_t CC1101_OOK_PA_TABLE_433[2] = {0x00, 0xC0};

/// Preset OOK para 433 MHz.
const cc1101OokConfig_t CC1101_OOK_CONFIG_433 = {
    .band = CC1101_BAND_433MHZ,
    .channel = 0,
    .deviceAddress = 0,
    .packetLength = 61,
    .variableLength = TRUE,
    .crcEnable = TRUE,
    .addressCheckEnable = FALSE,
    .syncWord = 0xD391,
    .dataRateBps = 38400,
    .rxBandwidthHz = 60000,
    .asyncSerialMode = FALSE,
    .paTable = CC1101_OOK_PA_TABLE_433,
    .paTableSize = 2,
    .paPowerIndex = 1,
};

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

/// Escribe una lista de registros internos.
/// \param config arreglo de pares dirección/valor
/// \param size cantidad de entradas
static bool_t cc1101_applyRegisterConfig(const cc1101RegisterSetting_t *config,
                                         uint8_t size) {
  uint8_t i;

  if (config == NULL || size == 0) {
    return FALSE;
  }

  for (i = 0; i < size; i++) {
    if (!cc1101_writeRegister(config[i].address, config[i].value)) {
      return FALSE;
    }
  }

  return TRUE;
}

/// Busca los bits CHANBW_E/M más cercanos para el ancho de banda deseado.
/// \param targetBwHz ancho de banda objetivo en Hz
/// \param mdmcfg4BwBits salida con bits [7:4] de MDMCFG4
static void cc1101_encodeRxBandwidth(uint32_t targetBwHz,
                                     uint8_t *mdmcfg4BwBits) {
  uint8_t e, m;
  uint32_t bestBw = 0;
  uint32_t bestError = 0xFFFFFFFFUL;
  uint8_t bestBits = 0;

  for (e = 0; e < 4; e++) {
    for (m = 0; m < 4; m++) {
      uint32_t bw = CC1101_XOSC_HZ / (8UL * (4UL + m) * (1UL << e));
      uint32_t error =
          (bw > targetBwHz) ? (bw - targetBwHz) : (targetBwHz - bw);
      if (error < bestError) {
        bestError = error;
        bestBw = bw;
        bestBits = (uint8_t)((e << 6) | (m << 4));
      }
    }
  }

  (void)bestBw;
  *mdmcfg4BwBits = bestBits;
}

/// Busca DRATE_E/M para aproximar el data rate solicitado.
/// \param targetBps data rate objetivo en bps
/// \param mdmcfg4RateBits salida con bits [3:0] de MDMCFG4
/// \param mdmcfg3RateBits salida con bits [7:0] de MDMCFG3
static void cc1101_encodeDataRate(uint32_t targetBps, uint8_t *mdmcfg4RateBits,
                                  uint8_t *mdmcfg3RateBits) {
  uint8_t e;
  uint16_t m;
  uint32_t bestError = 0xFFFFFFFFUL;
  uint8_t bestE = 0;
  uint8_t bestM = 0;

  for (e = 0; e < 16; e++) {
    for (m = 0; m < 256; m++) {
      uint64_t numerator = (uint64_t)(256UL + m) * (1UL << e) * CC1101_XOSC_HZ;
      uint32_t rate = (uint32_t)(numerator >> 28);
      uint32_t error =
          (rate > targetBps) ? (rate - targetBps) : (targetBps - rate);

      if (error < bestError) {
        bestError = error;
        bestE = e;
        bestM = (uint8_t)m;
      }
    }
  }

  *mdmcfg4RateBits = bestE & 0x0F;
  *mdmcfg3RateBits = bestM;
}

/// Obtiene registros de frecuencia para 433 MHz.
/// \param freq2 salida FREQ2
/// \param freq1 salida FREQ1
/// \param freq0 salida FREQ0
static void cc1101_frequencyRegisters(uint8_t *freq2, uint8_t *freq1,
                                      uint8_t *freq0) {
  *freq2 = 0x10;
  *freq1 = 0xB0;
  *freq0 = 0x71;
}

/// Inicializa pines, resetea el CC1101 y aplica una configuración base.
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

  // Aplicar configuración base OOK para la banda de 433 MHz
  if (!cc1101_applyOokConfig(&CC1101_OOK_CONFIG_433)) {
#ifdef MEDIUM_DEBUG
    printf("[drivers] [cc1101] ERROR: config base 433 fallo\r\n");
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
  printf("[drivers] [cc1101] CC1101 inicializado (PART=0x%02X VER=0x%02X)\r\n",
         cc1101_readRegister(CC1101_PARTNUM),
         cc1101_readRegister(CC1101_VERSION));
#endif

  return TRUE;
}

/// Ejecuta la secuencia de reset por SPI recomendada para el CC1101.
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

/// Traduce una configuración OOK conceptual y la aplica a registros CC1101.
/// \param config configuración OOK de alto nivel
bool_t cc1101_applyOokConfig(const cc1101OokConfig_t *config) {
  uint8_t mdmcfg4BwBits;
  uint8_t mdmcfg4RateBits;
  uint8_t mdmcfg3RateBits;
  uint8_t freq2, freq1, freq0;
  uint8_t pktctrl1, pktctrl0;
  uint8_t frend0;

  static const cc1101RegisterSetting_t commonRegisters[] = {
      {CC1101_IOCFG2, 0x29},   {CC1101_IOCFG1, 0x2E},   {CC1101_IOCFG0, 0x06},
      {CC1101_FIFOTHR, 0x47},  {CC1101_FSCTRL1, 0x06},  {CC1101_FSCTRL0, 0x00},
      {CC1101_MDMCFG1, 0x22},  {CC1101_MDMCFG0, 0xF8},  {CC1101_MCSM2, 0x07},
      {CC1101_MCSM1, 0x30},    {CC1101_MCSM0, 0x18},    {CC1101_FOCCFG, 0x16},
      {CC1101_BSCFG, 0x6C},    {CC1101_AGCCTRL2, 0x04}, {CC1101_AGCCTRL1, 0x00},
      {CC1101_AGCCTRL0, 0x92}, {CC1101_FREND1, 0x56},   {CC1101_FSCAL3, 0xE9},
      {CC1101_FSCAL2, 0x2A},   {CC1101_FSCAL1, 0x00},   {CC1101_FSCAL0, 0x1F},
      {CC1101_TEST2, 0x81},    {CC1101_TEST1, 0x35},    {CC1101_TEST0, 0x09},
  };

  if (config == NULL || config->paTable == NULL || config->paTableSize == 0 ||
      config->paPowerIndex >= config->paTableSize || config->dataRateBps == 0 ||
      config->rxBandwidthHz == 0) {
#ifdef MEDIUM_DEBUG
    printf("[drivers] [cc1101] ERROR: config OOK invalida\r\n");
#endif
    return FALSE;
  }

  cc1101_encodeRxBandwidth(config->rxBandwidthHz, &mdmcfg4BwBits);
  cc1101_encodeDataRate(config->dataRateBps, &mdmcfg4RateBits,
                        &mdmcfg3RateBits);
  cc1101_frequencyRegisters(&freq2, &freq1, &freq0);

  pktctrl1 = config->asyncSerialMode
                 ? 0x00
                 : (config->addressCheckEnable ? 0x05 : 0x04);
  pktctrl0 = config->asyncSerialMode ? 0x30
                                     : ((config->variableLength ? 0x01 : 0x00) |
                                        (config->crcEnable ? 0x04 : 0x00));
  frend0 = 0x10 | (config->paPowerIndex & 0x07);

  if (!cc1101_enterIdle()) {
#ifdef MEDIUM_DEBUG
    printf("[drivers] [cc1101] ERROR: no se pudo entrar en IDLE\r\n");
#endif
    return FALSE;
  }

  if (!cc1101_applyRegisterConfig(commonRegisters,
                                  sizeof(commonRegisters) /
                                      sizeof(commonRegisters[0]))) {
#ifdef MEDIUM_DEBUG
    printf("[drivers] [cc1101] ERROR: applyRegisterConfig fallo\r\n");
#endif
    return FALSE;
  }

  if (!cc1101_writeRegister(CC1101_IOCFG0,
                            config->asyncSerialMode ? 0x0D : 0x06) ||
      !cc1101_writeRegister(CC1101_SYNC1, (uint8_t)(config->syncWord >> 8)) ||
      !cc1101_writeRegister(CC1101_SYNC0, (uint8_t)(config->syncWord & 0xFF)) ||
      !cc1101_writeRegister(CC1101_PKTLEN, config->packetLength) ||
      !cc1101_writeRegister(CC1101_PKTCTRL1, pktctrl1) ||
      !cc1101_writeRegister(CC1101_PKTCTRL0, pktctrl0) ||
      !cc1101_writeRegister(CC1101_ADDR, config->deviceAddress) ||
      !cc1101_writeRegister(CC1101_CHANNR, config->channel) ||
      !cc1101_writeRegister(CC1101_FREQ2, freq2) ||
      !cc1101_writeRegister(CC1101_FREQ1, freq1) ||
      !cc1101_writeRegister(CC1101_FREQ0, freq0) ||
      !cc1101_writeRegister(CC1101_MDMCFG4, mdmcfg4BwBits | mdmcfg4RateBits) ||
      !cc1101_writeRegister(CC1101_MDMCFG3, mdmcfg3RateBits) ||
      !cc1101_writeRegister(
          CC1101_MDMCFG2,
          0x30 | (config->asyncSerialMode ? 0x00 : CC1101_SYNC_MODE_30_32)) ||
      !cc1101_writeRegister(CC1101_DEVIATN, 0x00) ||
      !cc1101_writeRegister(CC1101_FREND0, frend0)) {
#ifdef MEDIUM_DEBUG
    printf("[drivers] [cc1101] ERROR: escritura de registros OOK fallo\r\n");
#endif
    return FALSE;
  }

  if (!cc1101_writePaTable(config->paTable, config->paTableSize)) {
#ifdef MEDIUM_DEBUG
    printf("[drivers] [cc1101] ERROR: escritura PATABLE fallo\r\n");
#endif
    return FALSE;
  }

#ifdef MEDIUM_DEBUG
  printf(
      "[drivers] [cc1101] OOK cfg OK band=433 ch=%u rate=%lu bw=%luHz async=%u "
      "sync=0x%04X pklen=%u\r\n",
      config->channel, (unsigned long)config->dataRateBps,
      (unsigned long)config->rxBandwidthHz, config->asyncSerialMode,
      config->syncWord, config->packetLength);
#endif

  return TRUE;
}

/// Escribe un registro simple del CC1101.
/// \param address dirección del registro
/// \param value valor a escribir
bool_t cc1101_writeRegister(uint8_t address, uint8_t value) {
  uint8_t buffer[2] = {address, value};

  if (!cc1101_beginTransaction()) {
    return FALSE;
  }

  spiWrite(SPI0, buffer, sizeof(buffer));
  cc1101_endTransaction();
  return TRUE;
}

/// Lee un registro de configuración o estado del CC1101.
/// \param address dirección del registro
/// \return valor leído
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

/// Escribe múltiples bytes consecutivos (modo burst).
/// \param address dirección base
/// \param data datos a escribir
/// \param size cantidad de bytes
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

/// Lee múltiples bytes consecutivos (modo burst).
/// \param address dirección base
/// \param data buffer de salida
/// \param size cantidad de bytes
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

/// Envía un comando strobe al CC1101.
/// \param strobe comando a ejecutar
bool_t cc1101_commandStrobe(uint8_t strobe) {
  uint8_t command = strobe;

  if (!cc1101_beginTransaction()) {
    return FALSE;
  }

  spiWrite(SPI0, &command, 1);
  cc1101_endTransaction();
  return TRUE;
}

/// Cambia el estado del CC1101 a RX.
bool_t cc1101_enterRx(void) { return cc1101_commandStrobe(CC1101_SRX); }

/// Cambia el estado del CC1101 a TX.
bool_t cc1101_enterTx(void) { return cc1101_commandStrobe(CC1101_STX); }

/// Cambia el estado del CC1101 a IDLE.
bool_t cc1101_enterIdle(void) { return cc1101_commandStrobe(CC1101_SIDLE); }

/// Vacía el FIFO de recepción.
bool_t cc1101_flushRxFifo(void) {
  if (!cc1101_enterIdle()) {
    return FALSE;
  }
  return cc1101_commandStrobe(CC1101_SFRX);
}

/// Vacía el FIFO de transmisión.
bool_t cc1101_flushTxFifo(void) {
  if (!cc1101_enterIdle()) {
    return FALSE;
  }
  return cc1101_commandStrobe(CC1101_SFTX);
}

/// Configura el canal de RF.
/// \param channel número de canal
bool_t cc1101_setChannel(uint8_t channel) {
  return cc1101_writeRegister(CC1101_CHANNR, channel);
}

/// Configura la dirección de nodo para filtrado en modo paquete.
/// \param address dirección de dispositivo
bool_t cc1101_setDeviceAddress(uint8_t address) {
  return cc1101_writeRegister(CC1101_ADDR, address);
}

/// Escribe la tabla de potencia (PATABLE).
/// \param paTable tabla de niveles de potencia
/// \param size cantidad de niveles
bool_t cc1101_writePaTable(const uint8_t *paTable, uint8_t size) {
  return cc1101_writeBurstRegister(CC1101_PATABLE, paTable, size);
}

/// Copia un payload al FIFO de transmisión.
/// \param data buffer con el payload
/// \param size cantidad de bytes
bool_t cc1101_writeTxFifo(const uint8_t *data, uint8_t size) {
  return cc1101_writeBurstRegister(CC1101_FIFO, data, size);
}

/// Extrae datos del FIFO de recepción.
/// \param data buffer de salida
/// \param size cantidad de bytes a leer
bool_t cc1101_readRxFifo(uint8_t *data, uint8_t size) {
  return cc1101_readBurstRegister(CC1101_FIFO, data, size);
}

/// Lee cantidad de bytes disponibles en RX FIFO.
uint8_t cc1101_rxBytes(void) {
  return cc1101_readRegister(CC1101_RXBYTES) & 0x7F;
}

/// Lee cantidad de bytes cargados en TX FIFO.
uint8_t cc1101_txBytes(void) {
  return cc1101_readRegister(CC1101_TXBYTES) & 0x7F;
}

/// Lee el estado lógico del pin GDO0.
bool_t cc1101_gdo0Read(void) { return gpioRead(CC1101_GDO0_PIN); }

/// Lee el estado lógico del pin GDO2.
bool_t cc1101_gdo2Read(void) { return gpioRead(CC1101_GDO2_PIN); }
