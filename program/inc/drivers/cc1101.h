//===----------------------------------------------------------------------===//
///
/// \file
/// Definiciones para utilzar un módulo de radiofrecuencia CC1101 por SPI.
///
//===----------------------------------------------------------------------===//

#ifndef CC1101_H
#define CC1101_H

#include "main.h"

/// CS en GPIO0[15], pin de la EDU-CIAA-NXP ENET_TXD1
#define CC1101_CS_PIN ENET_TXD1
/// GDO0 en GPIO3[3], pin de la EDU-CIAA-NXP GPIO1
#define CC1101_GDO0_PIN GPIO1
/// GDO2 en GPIO3[0], pin de la EDU-CIAA-NXP GPIO0
#define CC1101_GDO2_PIN GPIO0

/// Bit para acceso de lectura
#define CC1101_READ_SINGLE 0x80
/// Bit para acceso burst
#define CC1101_BURST 0x40
/// Dirección de la tabla de potencia de salida
#define CC1101_PATABLE 0x3E
/// Dirección del FIFO de TX/RX
#define CC1101_FIFO 0x3F

/// Registros de configuración del CC1101
typedef enum {
  CC1101_IOCFG2 = 0x00,
  CC1101_IOCFG1 = 0x01,
  CC1101_IOCFG0 = 0x02,
  CC1101_FIFOTHR = 0x03,
  CC1101_SYNC1 = 0x04,
  CC1101_SYNC0 = 0x05,
  CC1101_PKTLEN = 0x06,
  CC1101_PKTCTRL1 = 0x07,
  CC1101_PKTCTRL0 = 0x08,
  CC1101_ADDR = 0x09,
  CC1101_CHANNR = 0x0A,
  CC1101_FSCTRL1 = 0x0B,
  CC1101_FSCTRL0 = 0x0C,
  CC1101_FREQ2 = 0x0D,
  CC1101_FREQ1 = 0x0E,
  CC1101_FREQ0 = 0x0F,
  CC1101_MDMCFG4 = 0x10,
  CC1101_MDMCFG3 = 0x11,
  CC1101_MDMCFG2 = 0x12,
  CC1101_MDMCFG1 = 0x13,
  CC1101_MDMCFG0 = 0x14,
  CC1101_DEVIATN = 0x15,
  CC1101_MCSM2 = 0x16,
  CC1101_MCSM1 = 0x17,
  CC1101_MCSM0 = 0x18,
  CC1101_FOCCFG = 0x19,
  CC1101_BSCFG = 0x1A,
  CC1101_AGCCTRL2 = 0x1B,
  CC1101_AGCCTRL1 = 0x1C,
  CC1101_AGCCTRL0 = 0x1D,
  CC1101_WOREVT1 = 0x1E,
  CC1101_WOREVT0 = 0x1F,
  CC1101_WORCTRL = 0x20,
  CC1101_FREND1 = 0x21,
  CC1101_FREND0 = 0x22,
  CC1101_FSCAL3 = 0x23,
  CC1101_FSCAL2 = 0x24,
  CC1101_FSCAL1 = 0x25,
  CC1101_FSCAL0 = 0x26,
  CC1101_RCCTRL1 = 0x27,
  CC1101_RCCTRL0 = 0x28,
  CC1101_FSTEST = 0x29,
  CC1101_PTEST = 0x2A,
  CC1101_AGCTEST = 0x2B,
  CC1101_TEST2 = 0x2C,
  CC1101_TEST1 = 0x2D,
  CC1101_TEST0 = 0x2E,
} cc1101Register_t;

/// Registros de estado del CC1101
typedef enum {
  CC1101_PARTNUM = 0x30,
  CC1101_VERSION = 0x31,
  CC1101_FREQEST = 0x32,
  CC1101_LQI = 0x33,
  CC1101_RSSI = 0x34,
  CC1101_MARCSTATE = 0x35,
  CC1101_WORTIME1 = 0x36,
  CC1101_WORTIME0 = 0x37,
  CC1101_PKTSTATUS = 0x38,
  CC1101_VCO_VC_DAC = 0x39,
  CC1101_TXBYTES = 0x3A,
  CC1101_RXBYTES = 0x3B,
  CC1101_RCCTRL1_STATUS = 0x3C,
  CC1101_RCCTRL0_STATUS = 0x3D,
} cc1101StatusRegister_t;

/// Comandos strobe del CC1101
typedef enum {
  CC1101_SRES = 0x30,
  CC1101_SFSTXON = 0x31,
  CC1101_SXOFF = 0x32,
  CC1101_SCAL = 0x33,
  CC1101_SRX = 0x34,
  CC1101_STX = 0x35,
  CC1101_SIDLE = 0x36,
  CC1101_SAFC = 0x37,
  CC1101_SWOR = 0x38,
  CC1101_SPWD = 0x39,
  CC1101_SFRX = 0x3A,
  CC1101_SFTX = 0x3B,
  CC1101_SWORRST = 0x3C,
  CC1101_SNOP = 0x3D,
} cc1101Strobe_t;

/// Banda ISM soportada por el preset OOK.
typedef enum {
  CC1101_BAND_433MHZ = 0,
} cc1101Band_t;

/// Configuración conceptual para OOK. Se traduce internamente a registros.
typedef struct {
  /// Banda de operación
  cc1101Band_t band;
  /// Canal de RF
  uint8_t channel;
  /// Dirección de nodo
  uint8_t deviceAddress;
  /// Largo de paquete máximo/fijo (según variableLength)
  uint8_t packetLength;
  /// Usa longitud variable de paquete
  bool_t variableLength;
  /// Habilita CRC en hardware
  bool_t crcEnable;
  /// Habilita filtro por dirección
  bool_t addressCheckEnable;
  /// Sync word de 16 bits
  uint16_t syncWord;
  /// Data rate objetivo en bits por segundo
  uint32_t dataRateBps;
  /// Ancho de banda RX objetivo en Hz
  uint32_t rxBandwidthHz;
  /// Habilita modo async serial (sin FIFO de paquetes)
  bool_t asyncSerialMode;
  /// Tabla de potencia para OOK (PATABLE)
  const uint8_t *paTable;
  /// Cantidad de entradas en PATABLE
  uint8_t paTableSize;
  /// Índice de PATABLE para transmitir el bit '1' en OOK
  uint8_t paPowerIndex;
} cc1101OokConfig_t;

/// PATABLE recomendada para OOK en 433 MHz (0 = off, 1 = nivel alto)
extern const uint8_t CC1101_OOK_PA_TABLE_433[2];

/// Preset OOK de referencia para 433 MHz
extern const cc1101OokConfig_t CC1101_OOK_CONFIG_433;

bool_t cc1101_init(void);
bool_t cc1101_reset(void);
bool_t cc1101_applyOokConfig(const cc1101OokConfig_t *config);

bool_t cc1101_writeRegister(uint8_t address, uint8_t value);
uint8_t cc1101_readRegister(uint8_t address);
bool_t cc1101_writeBurstRegister(uint8_t address, const uint8_t *data,
                                 uint8_t size);
bool_t cc1101_readBurstRegister(uint8_t address, uint8_t *data, uint8_t size);

bool_t cc1101_commandStrobe(uint8_t strobe);
bool_t cc1101_enterRx(void);
bool_t cc1101_enterTx(void);
bool_t cc1101_enterIdle(void);
bool_t cc1101_flushRxFifo(void);
bool_t cc1101_flushTxFifo(void);

bool_t cc1101_setChannel(uint8_t channel);
bool_t cc1101_setDeviceAddress(uint8_t address);
bool_t cc1101_writePaTable(const uint8_t *paTable, uint8_t size);

bool_t cc1101_writeTxFifo(const uint8_t *data, uint8_t size);
bool_t cc1101_readRxFifo(uint8_t *data, uint8_t size);
uint8_t cc1101_rxBytes(void);
uint8_t cc1101_txBytes(void);

bool_t cc1101_gdo0Read(void);
bool_t cc1101_gdo2Read(void);

#endif
