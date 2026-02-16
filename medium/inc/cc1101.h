/**
 * @file cc1101.h
 * @brief CC1101 RF transceiver driver for EDU-CIAA with sAPI
 *
 * Adapted from branch_rf ELECHOUSE_CC1101_SRC_DRV library.
 * Provides register definitions, SPI communication, and configuration
 * functions for the CC1101 sub-GHz transceiver.
 */

#ifndef _CC1101_H_
#define _CC1101_H_

#include "chip.h"
#include "sapi.h"

/*==================[register definitions]==================================*/

#define CC1101_IOCFG2   0x00
#define CC1101_IOCFG1   0x01
#define CC1101_IOCFG0   0x02
#define CC1101_FIFOTHR  0x03
#define CC1101_SYNC1    0x04
#define CC1101_SYNC0    0x05
#define CC1101_PKTLEN   0x06
#define CC1101_PKTCTRL1 0x07
#define CC1101_PKTCTRL0 0x08
#define CC1101_ADDR     0x09
#define CC1101_CHANNR   0x0A
#define CC1101_FSCTRL1  0x0B
#define CC1101_FSCTRL0  0x0C
#define CC1101_FREQ2    0x0D
#define CC1101_FREQ1    0x0E
#define CC1101_FREQ0    0x0F
#define CC1101_MDMCFG4  0x10
#define CC1101_MDMCFG3  0x11
#define CC1101_MDMCFG2  0x12
#define CC1101_MDMCFG1  0x13
#define CC1101_MDMCFG0  0x14
#define CC1101_DEVIATN  0x15
#define CC1101_MCSM2    0x16
#define CC1101_MCSM1    0x17
#define CC1101_MCSM0    0x18
#define CC1101_FOCCFG   0x19
#define CC1101_BSCFG    0x1A
#define CC1101_AGCCTRL2 0x1B
#define CC1101_AGCCTRL1 0x1C
#define CC1101_AGCCTRL0 0x1D
#define CC1101_WOREVT1  0x1E
#define CC1101_WOREVT0  0x1F
#define CC1101_WORCTRL  0x20
#define CC1101_FREND1   0x21
#define CC1101_FREND0   0x22
#define CC1101_FSCAL3   0x23
#define CC1101_FSCAL2   0x24
#define CC1101_FSCAL1   0x25
#define CC1101_FSCAL0   0x26
#define CC1101_RCCTRL1  0x27
#define CC1101_RCCTRL0  0x28
#define CC1101_FSTEST   0x29
#define CC1101_PTEST    0x2A
#define CC1101_AGCTEST  0x2B
#define CC1101_TEST2    0x2C
#define CC1101_TEST1    0x2D
#define CC1101_TEST0    0x2E
#define CC1101_PATABLE  0x3E

/*==================[strobe commands]=======================================*/

#define CC1101_SRES    0x30
#define CC1101_SFSTXON 0x31
#define CC1101_SXOFF   0x32
#define CC1101_SCAL    0x33
#define CC1101_SRX     0x34
#define CC1101_STX     0x35
#define CC1101_SIDLE   0x36
#define CC1101_SWOR    0x38
#define CC1101_SPWD    0x39
#define CC1101_SFRX    0x3A
#define CC1101_SFTX    0x3B
#define CC1101_SWORRST 0x3C
#define CC1101_SNOP    0x3D

/*==================[status registers]======================================*/

#define CC1101_PARTNUM    0x30
#define CC1101_VERSION    0x31
#define CC1101_FREQEST    0x32
#define CC1101_LQI        0x33
#define CC1101_RSSI       0x34
#define CC1101_MARCSTATE  0x35
#define CC1101_WORTIME1   0x36
#define CC1101_WORTIME0   0x37
#define CC1101_PKTSTATUS  0x38
#define CC1101_VCO_VC_DAC 0x39
#define CC1101_TXBYTES    0x3A
#define CC1101_RXBYTES    0x3B

/*==================[SPI modes]=============================================*/

#define WRITE_BURST 0x40
#define READ_SINGLE 0x80
#define READ_BURST  0xC0

/*==================[modulation types]======================================*/

#define MOD_2FSK    0
#define MOD_GFSK    1
#define MOD_ASK_OOK 2
#define MOD_4FSK    3
#define MOD_MSK     4

/*==================[types]=================================================*/

typedef struct {
    float freq_mhz;
    uint8_t modulation_mode;
    uint8_t m4RxBw;
    uint8_t m4DaRa;
    uint8_t m2DCOFF;
    uint8_t m2MODFM;
    uint8_t m2MANCH;
    uint8_t m2SYNCM;
    uint8_t pc0WDATA;
    uint8_t pc0PktForm;
    uint8_t pc0CRC_EN;
    uint8_t pc0LenConf;
    uint8_t frend0_val;
    int8_t pa_power;
} cc1101_config_t;

/*==================[external data]=========================================*/

extern cc1101_config_t cc1101;

/*==================[GPIO functions (implemented in rf_capture.c)]==========*/

void cc1101_setCS(bool_t state);
void cc1101_setGDO0(bool_t state);
bool_t cc1101_getGDO0(void);

/*==================[SPI functions]=========================================*/

void cc1101_writeReg(uint8_t addr, uint8_t value);
uint8_t cc1101_readReg(uint8_t addr);
void cc1101_sendStrobe(uint8_t strobe);
void cc1101_writeBurstReg(uint8_t addr, uint8_t *buffer, uint8_t num);
uint8_t cc1101_readStatus(uint8_t addr);

/*==================[init and config]=======================================*/

void cc1101_init(void);
void cc1101_reset(void);
bool_t cc1101_detect(void);

void cc1101_setSidle(void);
void cc1101_setModulation(uint8_t mode);
void cc1101_setFrequency(float freqMHz);
void cc1101_setRxBW(float bandwidth_khz);
void cc1101_setDataRate(float rate_kbps);
void cc1101_setPktFormat(uint8_t format);
void cc1101_setSyncMode(uint8_t mode);
void cc1101_setWhiteData(bool_t enable);
void cc1101_setCrc(bool_t enable);
void cc1101_setPower(int8_t power_dbm);

/*==================[operation modes]=======================================*/

void cc1101_setRxMode(void);
void cc1101_setTxMode(void);

/*==================[end of file]===========================================*/

#endif /* _CC1101_H_ */
