/**
 * @file cc1101.c
 * @brief CC1101 RF transceiver driver implementation
 *
 * Adapted from branch_rf ELECHOUSE_CC1101_SRC_DRV library for EDU-CIAA.
 * SPI communication, register configuration, frequency/modulation setup.
 */

#include "cc1101.h"

/*==================[PA power tables]=======================================*/

static const uint8_t PA_TABLE_315[8] = {0x12, 0x0D, 0x1C, 0x34,
                                        0x51, 0x85, 0xCB, 0xC2};
static const uint8_t PA_TABLE_433[8] = {0x12, 0x0E, 0x1D, 0x34,
                                        0x60, 0x84, 0xC8, 0xC0};
static const uint8_t PA_TABLE_868[10] = {0x03, 0x17, 0x1D, 0x26, 0x37,
                                         0x50, 0x86, 0xCD, 0xC5, 0xC0};
static const uint8_t PA_TABLE_915[10] = {0x03, 0x0E, 0x1E, 0x27, 0x38,
                                         0x8E, 0x84, 0xCC, 0xC3, 0xC0};

/*==================[global config]=========================================*/

cc1101_config_t cc1101 = {
    .freq_mhz = 433.92,
    .modulation_mode = MOD_ASK_OOK,
    .m4RxBw = 0,
    .m4DaRa = 0,
    .m2DCOFF = 0,
    .m2MODFM = 0x30,
    .m2MANCH = 0,
    .m2SYNCM = 0,
    .pc0WDATA = 0,
    .pc0PktForm = 0x30,
    .pc0CRC_EN = 0,
    .pc0LenConf = 0,
    .frend0_val = 0x11,
    .pa_power = 12
};

/*==================[private functions]=====================================*/

static void cc1101_split_MDMCFG2(void) {
    int calc = cc1101_readReg(CC1101_MDMCFG2);
    cc1101.m2DCOFF = 0;
    cc1101.m2MODFM = 0;
    cc1101.m2MANCH = 0;
    cc1101.m2SYNCM = 0;

    bool_t done = FALSE;
    while (!done) {
        if (calc >= 128) {
            calc -= 128;
            cc1101.m2DCOFF += 128;
        } else if (calc >= 16) {
            calc -= 16;
            cc1101.m2MODFM += 16;
        } else if (calc >= 8) {
            calc -= 8;
            cc1101.m2MANCH += 8;
        } else {
            cc1101.m2SYNCM = calc;
            done = TRUE;
        }
    }
}

static void cc1101_split_MDMCFG4(void) {
    int calc = cc1101_readReg(CC1101_MDMCFG4);
    cc1101.m4RxBw = 0;
    cc1101.m4DaRa = 0;

    bool_t done = FALSE;
    while (!done) {
        if (calc >= 64) {
            calc -= 64;
            cc1101.m4RxBw += 64;
        } else if (calc >= 16) {
            calc -= 16;
            cc1101.m4RxBw += 16;
        } else {
            cc1101.m4DaRa = calc;
            done = TRUE;
        }
    }
}

static void cc1101_split_PKTCTRL0(void) {
    int calc = cc1101_readReg(CC1101_PKTCTRL0);
    cc1101.pc0WDATA = 0;
    cc1101.pc0PktForm = 0;
    cc1101.pc0CRC_EN = 0;
    cc1101.pc0LenConf = 0;

    bool_t done = FALSE;
    while (!done) {
        if (calc >= 64) {
            calc -= 64;
            cc1101.pc0WDATA += 64;
        } else if (calc >= 16) {
            calc -= 16;
            cc1101.pc0PktForm += 16;
        } else if (calc >= 4) {
            calc -= 4;
            cc1101.pc0CRC_EN += 4;
        } else {
            cc1101.pc0LenConf = calc;
            done = TRUE;
        }
    }
}

static void cc1101_setPA(int8_t p) {
    cc1101.pa_power = p;
    uint8_t pa_table[8] = {0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t a = 0xC0;

    if (cc1101.freq_mhz >= 300 && cc1101.freq_mhz <= 348) {
        if (p <= -30)       a = PA_TABLE_315[0];
        else if (p <= -20)  a = PA_TABLE_315[1];
        else if (p <= -15)  a = PA_TABLE_315[2];
        else if (p <= -10)  a = PA_TABLE_315[3];
        else if (p <= 0)    a = PA_TABLE_315[4];
        else if (p <= 5)    a = PA_TABLE_315[5];
        else if (p <= 7)    a = PA_TABLE_315[6];
        else                a = PA_TABLE_315[7];
    } else if (cc1101.freq_mhz >= 378 && cc1101.freq_mhz <= 464) {
        if (p <= -30)       a = PA_TABLE_433[0];
        else if (p <= -20)  a = PA_TABLE_433[1];
        else if (p <= -15)  a = PA_TABLE_433[2];
        else if (p <= -10)  a = PA_TABLE_433[3];
        else if (p <= 0)    a = PA_TABLE_433[4];
        else if (p <= 5)    a = PA_TABLE_433[5];
        else if (p <= 7)    a = PA_TABLE_433[6];
        else                a = PA_TABLE_433[7];
    } else if (cc1101.freq_mhz >= 779 && cc1101.freq_mhz <= 899.99) {
        if (p <= -30)       a = PA_TABLE_868[0];
        else if (p <= -20)  a = PA_TABLE_868[1];
        else if (p <= -15)  a = PA_TABLE_868[2];
        else if (p <= -10)  a = PA_TABLE_868[3];
        else if (p <= -6)   a = PA_TABLE_868[4];
        else if (p <= 0)    a = PA_TABLE_868[5];
        else if (p <= 5)    a = PA_TABLE_868[6];
        else if (p <= 7)    a = PA_TABLE_868[7];
        else if (p <= 10)   a = PA_TABLE_868[8];
        else                a = PA_TABLE_868[9];
    } else if (cc1101.freq_mhz >= 900 && cc1101.freq_mhz <= 928) {
        if (p <= -30)       a = PA_TABLE_915[0];
        else if (p <= -20)  a = PA_TABLE_915[1];
        else if (p <= -15)  a = PA_TABLE_915[2];
        else if (p <= -10)  a = PA_TABLE_915[3];
        else if (p <= -6)   a = PA_TABLE_915[4];
        else if (p <= 0)    a = PA_TABLE_915[5];
        else if (p <= 5)    a = PA_TABLE_915[6];
        else if (p <= 7)    a = PA_TABLE_915[7];
        else if (p <= 10)   a = PA_TABLE_915[8];
        else                a = PA_TABLE_915[9];
    }

    if (cc1101.modulation_mode == MOD_ASK_OOK) {
        pa_table[0] = 0;
        pa_table[1] = a;
    } else {
        pa_table[0] = a;
        pa_table[1] = 0;
    }

    cc1101_writeBurstReg(CC1101_PATABLE, pa_table, 8);
}

static uint8_t map_value(int x, int in_min, int in_max, int out_min,
                         int out_max) {
    return (uint8_t)((x - in_min) * (out_max - out_min) / (in_max - in_min) +
                     out_min);
}

static void cc1101_calibrate(void) {
    uint8_t clb1_min = 24, clb1_max = 28;
    uint8_t clb2_min = 31, clb2_max = 38;
    uint8_t clb3_min = 65, clb3_max = 76;
    uint8_t clb4_min = 77, clb4_max = 79;

    if (cc1101.freq_mhz >= 300 && cc1101.freq_mhz <= 348) {
        cc1101_writeReg(CC1101_FSCTRL0, map_value((int)cc1101.freq_mhz, 300, 348,
                                                   clb1_min, clb1_max));
        if (cc1101.freq_mhz < 322.88) {
            cc1101_writeReg(CC1101_TEST0, 0x0B);
        } else {
            cc1101_writeReg(CC1101_TEST0, 0x09);
            uint8_t s = cc1101_readStatus(CC1101_FSCAL2);
            if (s < 32) cc1101_writeReg(CC1101_FSCAL2, s + 32);
        }
    } else if (cc1101.freq_mhz >= 378 && cc1101.freq_mhz <= 464) {
        cc1101_writeReg(CC1101_FSCTRL0, map_value((int)cc1101.freq_mhz, 378, 464,
                                                   clb2_min, clb2_max));
        if (cc1101.freq_mhz < 430.5) {
            cc1101_writeReg(CC1101_TEST0, 0x0B);
        } else {
            cc1101_writeReg(CC1101_TEST0, 0x09);
            uint8_t s = cc1101_readStatus(CC1101_FSCAL2);
            if (s < 32) cc1101_writeReg(CC1101_FSCAL2, s + 32);
        }
    } else if (cc1101.freq_mhz >= 779 && cc1101.freq_mhz <= 899.99) {
        cc1101_writeReg(CC1101_FSCTRL0, map_value((int)cc1101.freq_mhz, 779, 899,
                                                   clb3_min, clb3_max));
        if (cc1101.freq_mhz < 861) {
            cc1101_writeReg(CC1101_TEST0, 0x0B);
        } else {
            cc1101_writeReg(CC1101_TEST0, 0x09);
            uint8_t s = cc1101_readStatus(CC1101_FSCAL2);
            if (s < 32) cc1101_writeReg(CC1101_FSCAL2, s + 32);
        }
    } else if (cc1101.freq_mhz >= 900 && cc1101.freq_mhz <= 928) {
        cc1101_writeReg(CC1101_FSCTRL0, map_value((int)cc1101.freq_mhz, 900, 928,
                                                   clb4_min, clb4_max));
        cc1101_writeReg(CC1101_TEST0, 0x09);
        uint8_t s = cc1101_readStatus(CC1101_FSCAL2);
        if (s < 32) cc1101_writeReg(CC1101_FSCAL2, s + 32);
    }
}

static void cc1101_regConfigSettings(void) {
    cc1101_writeReg(CC1101_FSCTRL1,  0x06);
    cc1101_writeReg(CC1101_IOCFG2,   0x0D);
    cc1101_writeReg(CC1101_IOCFG0,   0x0D);
    cc1101_writeReg(CC1101_PKTCTRL0, 0x32);
    cc1101_writeReg(CC1101_MDMCFG3,  0x93);
    cc1101_writeReg(CC1101_MDMCFG4,  0x07);
    cc1101_writeReg(CC1101_MDMCFG1,  0x02);
    cc1101_writeReg(CC1101_MDMCFG0,  0xF8);
    cc1101_writeReg(CC1101_CHANNR,   0x00);
    cc1101_writeReg(CC1101_DEVIATN,  0x47);
    cc1101_writeReg(CC1101_FREND1,   0x56);
    cc1101_writeReg(CC1101_MCSM0,    0x18);
    cc1101_writeReg(CC1101_FOCCFG,   0x16);
    cc1101_writeReg(CC1101_BSCFG,    0x1C);
    cc1101_writeReg(CC1101_AGCCTRL2, 0xC7);
    cc1101_writeReg(CC1101_AGCCTRL1, 0x00);
    cc1101_writeReg(CC1101_AGCCTRL0, 0xB2);
    cc1101_writeReg(CC1101_FSCAL3,   0xE9);
    cc1101_writeReg(CC1101_FSCAL2,   0x2A);
    cc1101_writeReg(CC1101_FSCAL1,   0x00);
    cc1101_writeReg(CC1101_FSCAL0,   0x1F);
    cc1101_writeReg(CC1101_TEST2,    0x81);
    cc1101_writeReg(CC1101_TEST1,    0x35);
    cc1101_writeReg(CC1101_TEST0,    0x09);
    cc1101_writeReg(CC1101_PKTCTRL1, 0x04);
    cc1101_writeReg(CC1101_ADDR,     0x00);
}

/*==================[SPI functions]=========================================*/

void cc1101_writeReg(uint8_t addr, uint8_t value) {
    uint8_t buffer[2] = {addr, value};
    cc1101_setCS(FALSE);
    delayInaccurateUs(10);
    spiWrite(SPI0, buffer, 2);
    cc1101_setCS(TRUE);
}

uint8_t cc1101_readReg(uint8_t addr) {
    uint8_t temp = addr | READ_SINGLE;
    uint8_t value;
    cc1101_setCS(FALSE);
    delayInaccurateUs(10);
    spiWrite(SPI0, &temp, 1);
    spiRead(SPI0, &value, 1);
    cc1101_setCS(TRUE);
    return value;
}

void cc1101_sendStrobe(uint8_t strobe) {
    cc1101_setCS(FALSE);
    delayInaccurateUs(10);
    spiWrite(SPI0, &strobe, 1);
    cc1101_setCS(TRUE);
}

void cc1101_writeBurstReg(uint8_t addr, uint8_t *buffer, uint8_t num) {
    uint8_t temp = addr | WRITE_BURST;
    cc1101_setCS(FALSE);
    delayInaccurateUs(10);
    spiWrite(SPI0, &temp, 1);
    spiWrite(SPI0, buffer, num);
    cc1101_setCS(TRUE);
}

uint8_t cc1101_readStatus(uint8_t addr) {
    uint8_t temp = addr | READ_BURST;
    uint8_t value;
    cc1101_setCS(FALSE);
    delayInaccurateUs(10);
    spiWrite(SPI0, &temp, 1);
    spiRead(SPI0, &value, 1);
    cc1101_setCS(TRUE);
    return value;
}

/*==================[init functions]========================================*/

void cc1101_reset(void) {
    cc1101_setCS(LOW);
    delay(1);
    cc1101_setCS(HIGH);
    delay(1);
    cc1101_setCS(LOW);
    delayInaccurateUs(100);
    cc1101_sendStrobe(CC1101_SRES);
    delayInaccurateUs(100);
    cc1101_setCS(HIGH);
}

void cc1101_init(void) {
    cc1101_setCS(TRUE);
    delay(10);
    cc1101_reset();
    cc1101_regConfigSettings();
}

bool_t cc1101_detect(void) {
    uint8_t version = cc1101_readStatus(CC1101_VERSION);
    return (version > 0) ? TRUE : FALSE;
}

/*==================[configuration functions]===============================*/

void cc1101_setSidle(void) {
    cc1101_sendStrobe(CC1101_SIDLE);
}

void cc1101_setModulation(uint8_t mode) {
    if (mode > 4) mode = 4;
    cc1101.modulation_mode = mode;
    cc1101_split_MDMCFG2();

    switch (mode) {
    case MOD_2FSK:
        cc1101.m2MODFM = 0x00;
        cc1101.frend0_val = 0x10;
        break;
    case MOD_GFSK:
        cc1101.m2MODFM = 0x10;
        cc1101.frend0_val = 0x10;
        break;
    case MOD_ASK_OOK:
        cc1101.m2MODFM = 0x30;
        cc1101.frend0_val = 0x11;
        break;
    case MOD_4FSK:
        cc1101.m2MODFM = 0x40;
        cc1101.frend0_val = 0x10;
        break;
    case MOD_MSK:
        cc1101.m2MODFM = 0x70;
        cc1101.frend0_val = 0x10;
        break;
    }

    cc1101_writeReg(CC1101_MDMCFG2,
                    cc1101.m2DCOFF + cc1101.m2MODFM + cc1101.m2MANCH + cc1101.m2SYNCM);
    cc1101_writeReg(CC1101_FREND0, cc1101.frend0_val);
    cc1101_setPA(cc1101.pa_power);
}

void cc1101_setFrequency(float freqMHz) {
    cc1101.freq_mhz = freqMHz;
    uint32_t freq_val = (uint32_t)(freqMHz * 65536.0 / 26.0);

    cc1101_writeReg(CC1101_FREQ2, (uint8_t)((freq_val >> 16) & 0xFF));
    cc1101_writeReg(CC1101_FREQ1, (uint8_t)((freq_val >> 8) & 0xFF));
    cc1101_writeReg(CC1101_FREQ0, (uint8_t)(freq_val & 0xFF));

    cc1101_calibrate();
}

void cc1101_setRxBW(float bandwidth_khz) {
    cc1101_split_MDMCFG4();
    int s1 = 3;
    int s2 = 3;
    float f = bandwidth_khz;

    for (int i = 0; i < 3; i++) {
        if (f > 101.5625) { f /= 2; s1--; }
        else break;
    }
    for (int i = 0; i < 3; i++) {
        if (f > 58.1) { f /= 1.25; s2--; }
        else break;
    }

    s1 *= 64;
    s2 *= 16;
    cc1101.m4RxBw = s1 + s2;
    cc1101_writeReg(CC1101_MDMCFG4, cc1101.m4RxBw + cc1101.m4DaRa);
}

void cc1101_setDataRate(float rate_kbps) {
    cc1101_split_MDMCFG4();
    float c = rate_kbps;
    uint8_t MDMCFG3 = 0;

    if (c > 1621.83)   c = 1621.83;
    if (c < 0.0247955) c = 0.0247955;

    cc1101.m4DaRa = 0;
    for (int i = 0; i < 20; i++) {
        if (c <= 0.0494942) {
            c = c - 0.0247955;
            c = c / 0.00009685;
            MDMCFG3 = (uint8_t)c;
            float s1 = (c - MDMCFG3) * 10;
            if (s1 >= 5) MDMCFG3++;
            break;
        } else {
            cc1101.m4DaRa++;
            c = c / 2;
        }
    }

    cc1101_writeReg(CC1101_MDMCFG4, cc1101.m4RxBw + cc1101.m4DaRa);
    cc1101_writeReg(CC1101_MDMCFG3, MDMCFG3);
}

void cc1101_setPktFormat(uint8_t format) {
    cc1101_split_PKTCTRL0();
    cc1101.pc0PktForm = 0;
    if (format > 3) format = 3;
    cc1101.pc0PktForm = format * 16;
    cc1101_writeReg(CC1101_PKTCTRL0, cc1101.pc0WDATA + cc1101.pc0PktForm +
                                      cc1101.pc0CRC_EN + cc1101.pc0LenConf);
}

void cc1101_setSyncMode(uint8_t mode) {
    cc1101_split_MDMCFG2();
    cc1101.m2SYNCM = 0;
    if (mode > 7) mode = 7;
    cc1101.m2SYNCM = mode;
    cc1101_writeReg(CC1101_MDMCFG2,
                    cc1101.m2DCOFF + cc1101.m2MODFM + cc1101.m2MANCH + cc1101.m2SYNCM);
}

void cc1101_setWhiteData(bool_t enable) {
    cc1101_split_PKTCTRL0();
    cc1101.pc0WDATA = 0;
    if (enable) cc1101.pc0WDATA = 64;
    cc1101_writeReg(CC1101_PKTCTRL0, cc1101.pc0WDATA + cc1101.pc0PktForm +
                                      cc1101.pc0CRC_EN + cc1101.pc0LenConf);
}

void cc1101_setCrc(bool_t enable) {
    cc1101_split_PKTCTRL0();
    cc1101.pc0CRC_EN = 0;
    if (enable) cc1101.pc0CRC_EN = 4;
    cc1101_writeReg(CC1101_PKTCTRL0, cc1101.pc0WDATA + cc1101.pc0PktForm +
                                      cc1101.pc0CRC_EN + cc1101.pc0LenConf);
}

void cc1101_setPower(int8_t power_dbm) {
    cc1101_setPA(power_dbm);
}

/*==================[operation modes]=======================================*/

void cc1101_setRxMode(void) {
    cc1101_sendStrobe(CC1101_SIDLE);
    cc1101_sendStrobe(CC1101_SRX);
}

void cc1101_setTxMode(void) {
    cc1101_sendStrobe(CC1101_SIDLE);
    cc1101_sendStrobe(CC1101_STX);
}

/*==================[end of file]===========================================*/
