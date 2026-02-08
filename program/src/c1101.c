//===----------------------------------------------------------------------===//
///
/// \file c1101.c
/// Implementación de librería CC1101 para EDU-CIAA con sAPI
/// Basada en la librería ELECHOUSE_CC1101_SRC_DRV.h para Arduino/ESP32
///
//===----------------------------------------------------------------------===//

#include "c1101.h"

// ============ TABLAS DE POTENCIA PA ============

static const uint8_t PA_TABLE_315[8] = {0x12, 0x0D, 0x1C, 0x34,
                                        0x51, 0x85, 0xCB, 0xC2};
static const uint8_t PA_TABLE_433[8] = {0x12, 0x0E, 0x1D, 0x34,
                                        0x60, 0x84, 0xC8, 0xC0};
static const uint8_t PA_TABLE_868[10] = {0x03, 0x17, 0x1D, 0x26, 0x37,
                                         0x50, 0x86, 0xCD, 0xC5, 0xC0};
static const uint8_t PA_TABLE_915[10] = {0x03, 0x0E, 0x1E, 0x27, 0x38,
                                         0x8E, 0x84, 0xCC, 0xC3, 0xC0};

// ============ VARIABLE GLOBAL DE CONFIGURACIÓN ============

c1101_config_t c1101 = {.freq_mhz = 433.92,
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
                        .pa_power = 12};

// ============ FUNCIONES PRIVADAS ============

static void c1101_split_MDMCFG2(void) {
  int calc = c1101_readReg(CC1101_MDMCFG2);
  c1101.m2DCOFF = 0;
  c1101.m2MODFM = 0;
  c1101.m2MANCH = 0;
  c1101.m2SYNCM = 0;

  bool_t done = FALSE;
  while (!done) {
    if (calc >= 128) {
      calc -= 128;
      c1101.m2DCOFF += 128;
    } else if (calc >= 16) {
      calc -= 16;
      c1101.m2MODFM += 16;
    } else if (calc >= 8) {
      calc -= 8;
      c1101.m2MANCH += 8;
    } else {
      c1101.m2SYNCM = calc;
      done = TRUE;
    }
  }
}

static void c1101_split_MDMCFG4(void) {
  int calc = c1101_readReg(CC1101_MDMCFG4);
  c1101.m4RxBw = 0;
  c1101.m4DaRa = 0;

  bool_t done = FALSE;
  while (!done) {
    if (calc >= 64) {
      calc -= 64;
      c1101.m4RxBw += 64;
    } else if (calc >= 16) {
      calc -= 16;
      c1101.m4RxBw += 16;
    } else {
      c1101.m4DaRa = calc;
      done = TRUE;
    }
  }
}

static void c1101_split_PKTCTRL0(void) {
  int calc = c1101_readReg(CC1101_PKTCTRL0);
  c1101.pc0WDATA = 0;
  c1101.pc0PktForm = 0;
  c1101.pc0CRC_EN = 0;
  c1101.pc0LenConf = 0;

  bool_t done = FALSE;
  while (!done) {
    if (calc >= 64) {
      calc -= 64;
      c1101.pc0WDATA += 64;
    } else if (calc >= 16) {
      calc -= 16;
      c1101.pc0PktForm += 16;
    } else if (calc >= 4) {
      calc -= 4;
      c1101.pc0CRC_EN += 4;
    } else {
      c1101.pc0LenConf = calc;
      done = TRUE;
    }
  }
}

static void c1101_setPA(int8_t p) {
  c1101.pa_power = p;
  uint8_t pa_table[8] = {0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t a = 0xC0;

  if (c1101.freq_mhz >= 300 && c1101.freq_mhz <= 348) {
    if (p <= -30)
      a = PA_TABLE_315[0];
    else if (p <= -20)
      a = PA_TABLE_315[1];
    else if (p <= -15)
      a = PA_TABLE_315[2];
    else if (p <= -10)
      a = PA_TABLE_315[3];
    else if (p <= 0)
      a = PA_TABLE_315[4];
    else if (p <= 5)
      a = PA_TABLE_315[5];
    else if (p <= 7)
      a = PA_TABLE_315[6];
    else
      a = PA_TABLE_315[7];
  } else if (c1101.freq_mhz >= 378 && c1101.freq_mhz <= 464) {
    if (p <= -30)
      a = PA_TABLE_433[0];
    else if (p <= -20)
      a = PA_TABLE_433[1];
    else if (p <= -15)
      a = PA_TABLE_433[2];
    else if (p <= -10)
      a = PA_TABLE_433[3];
    else if (p <= 0)
      a = PA_TABLE_433[4];
    else if (p <= 5)
      a = PA_TABLE_433[5];
    else if (p <= 7)
      a = PA_TABLE_433[6];
    else
      a = PA_TABLE_433[7];
  } else if (c1101.freq_mhz >= 779 && c1101.freq_mhz <= 899.99) {
    if (p <= -30)
      a = PA_TABLE_868[0];
    else if (p <= -20)
      a = PA_TABLE_868[1];
    else if (p <= -15)
      a = PA_TABLE_868[2];
    else if (p <= -10)
      a = PA_TABLE_868[3];
    else if (p <= -6)
      a = PA_TABLE_868[4];
    else if (p <= 0)
      a = PA_TABLE_868[5];
    else if (p <= 5)
      a = PA_TABLE_868[6];
    else if (p <= 7)
      a = PA_TABLE_868[7];
    else if (p <= 10)
      a = PA_TABLE_868[8];
    else
      a = PA_TABLE_868[9];
  } else if (c1101.freq_mhz >= 900 && c1101.freq_mhz <= 928) {
    if (p <= -30)
      a = PA_TABLE_915[0];
    else if (p <= -20)
      a = PA_TABLE_915[1];
    else if (p <= -15)
      a = PA_TABLE_915[2];
    else if (p <= -10)
      a = PA_TABLE_915[3];
    else if (p <= -6)
      a = PA_TABLE_915[4];
    else if (p <= 0)
      a = PA_TABLE_915[5];
    else if (p <= 5)
      a = PA_TABLE_915[6];
    else if (p <= 7)
      a = PA_TABLE_915[7];
    else if (p <= 10)
      a = PA_TABLE_915[8];
    else
      a = PA_TABLE_915[9];
  }

  if (c1101.modulation_mode == MOD_ASK_OOK) {
    pa_table[0] = 0;
    pa_table[1] = a;
  } else {
    pa_table[0] = a;
    pa_table[1] = 0;
  }

  c1101_writeBurstReg(CC1101_PATABLE, pa_table, 8);
}

static uint8_t map_value(int x, int in_min, int in_max, int out_min,
                         int out_max) {
  return (uint8_t)((x - in_min) * (out_max - out_min) / (in_max - in_min) +
                   out_min);
}
static void c1101_calibrate(void) {
  uint8_t clb1_min = 24, clb1_max = 28;
  uint8_t clb2_min = 31, clb2_max = 38;
  uint8_t clb3_min = 65, clb3_max = 76;
  uint8_t clb4_min = 77, clb4_max = 79;

  if (c1101.freq_mhz >= 300 && c1101.freq_mhz <= 348) {
    c1101_writeReg(CC1101_FSCTRL0, map_value((int)c1101.freq_mhz, 300, 348,
                                             clb1_min, clb1_max));
    if (c1101.freq_mhz < 322.88) {
      c1101_writeReg(CC1101_TEST0, 0x0B);
    } else {
      c1101_writeReg(CC1101_TEST0, 0x09);
      uint8_t s = c1101_readStatus(CC1101_FSCAL2);
      if (s < 32)
        c1101_writeReg(CC1101_FSCAL2, s + 32);
    }
  } else if (c1101.freq_mhz >= 378 && c1101.freq_mhz <= 464) {
    c1101_writeReg(CC1101_FSCTRL0, map_value((int)c1101.freq_mhz, 378, 464,
                                             clb2_min, clb2_max));
    if (c1101.freq_mhz < 430.5) {
      c1101_writeReg(CC1101_TEST0, 0x0B);
    } else {
      c1101_writeReg(CC1101_TEST0, 0x09);
      uint8_t s = c1101_readStatus(CC1101_FSCAL2);
      if (s < 32)
        c1101_writeReg(CC1101_FSCAL2, s + 32);
    }
  } else if (c1101.freq_mhz >= 779 && c1101.freq_mhz <= 899.99) {
    c1101_writeReg(CC1101_FSCTRL0, map_value((int)c1101.freq_mhz, 779, 899,
                                             clb3_min, clb3_max));
    if (c1101.freq_mhz < 861) {
      c1101_writeReg(CC1101_TEST0, 0x0B);
    } else {
      c1101_writeReg(CC1101_TEST0, 0x09);
      uint8_t s = c1101_readStatus(CC1101_FSCAL2);
      if (s < 32)
        c1101_writeReg(CC1101_FSCAL2, s + 32);
    }
  } else if (c1101.freq_mhz >= 900 && c1101.freq_mhz <= 928) {
    c1101_writeReg(CC1101_FSCTRL0, map_value((int)c1101.freq_mhz, 900, 928,
                                             clb4_min, clb4_max));
    c1101_writeReg(CC1101_TEST0, 0x09);
    uint8_t s = c1101_readStatus(CC1101_FSCAL2);
    if (s < 32)
      c1101_writeReg(CC1101_FSCAL2, s + 32);
  }
}

static void c1101_regConfigSettings(void) {
  c1101_writeReg(CC1101_FSCTRL1, 0x06);
  c1101_writeReg(CC1101_IOCFG2, 0x0D);
  c1101_writeReg(CC1101_IOCFG0, 0x0D);
  c1101_writeReg(CC1101_PKTCTRL0, 0x32);
  c1101_writeReg(CC1101_MDMCFG3, 0x93);
  c1101_writeReg(CC1101_MDMCFG4, 0x07);
  c1101_writeReg(CC1101_MDMCFG1, 0x02);
  c1101_writeReg(CC1101_MDMCFG0, 0xF8);
  c1101_writeReg(CC1101_CHANNR, 0x00);
  c1101_writeReg(CC1101_DEVIATN, 0x47);
  c1101_writeReg(CC1101_FREND1, 0x56);
  c1101_writeReg(CC1101_MCSM0, 0x18);
  c1101_writeReg(CC1101_FOCCFG, 0x16);
  c1101_writeReg(CC1101_BSCFG, 0x1C);
  c1101_writeReg(CC1101_AGCCTRL2, 0xC7);
  c1101_writeReg(CC1101_AGCCTRL1, 0x00);
  c1101_writeReg(CC1101_AGCCTRL0, 0xB2);
  c1101_writeReg(CC1101_FSCAL3, 0xE9);
  c1101_writeReg(CC1101_FSCAL2, 0x2A);
  c1101_writeReg(CC1101_FSCAL1, 0x00);
  c1101_writeReg(CC1101_FSCAL0, 0x1F);
  c1101_writeReg(CC1101_TEST2, 0x81);
  c1101_writeReg(CC1101_TEST1, 0x35);
  c1101_writeReg(CC1101_TEST0, 0x09);
  c1101_writeReg(CC1101_PKTCTRL1, 0x04);
  c1101_writeReg(CC1101_ADDR, 0x00);
}

// ============ FUNCIONES SPI PÚBLICAS ============

void c1101_writeReg(uint8_t addr, uint8_t value) {
  uint8_t buffer[2] = {addr, value};
  c1101_setCS(FALSE);
  delayInaccurateUs(10);
  spiWrite(SPI0, buffer, 2);
  c1101_setCS(TRUE);
}

uint8_t c1101_readReg(uint8_t addr) {
  uint8_t temp = addr | READ_SINGLE;
  uint8_t value;
  c1101_setCS(FALSE);
  delayInaccurateUs(10);
  spiWrite(SPI0, &temp, 1);
  spiRead(SPI0, &value, 1);
  c1101_setCS(TRUE);
  return value;
}

void c1101_sendStrobe(uint8_t strobe) {
  c1101_setCS(FALSE);
  delayInaccurateUs(10);
  spiWrite(SPI0, &strobe, 1);
  c1101_setCS(TRUE);
}

void c1101_writeBurstReg(uint8_t addr, uint8_t *buffer, uint8_t num) {
  uint8_t temp = addr | WRITE_BURST;
  c1101_setCS(FALSE);
  delayInaccurateUs(10);
  spiWrite(SPI0, &temp, 1);
  spiWrite(SPI0, buffer, num);
  c1101_setCS(TRUE);
}

uint8_t c1101_readStatus(uint8_t addr) {
  uint8_t temp = addr | READ_BURST;
  uint8_t value;
  c1101_setCS(FALSE);
  delayInaccurateUs(10);
  spiWrite(SPI0, &temp, 1);
  spiRead(SPI0, &value, 1);
  c1101_setCS(TRUE);
  return value;
}

// ============ FUNCIONES PÚBLICAS DE INICIALIZACIÓN ============

void c1101_reset(void) {
  c1101_setCS(LOW);
  delay(1);
  c1101_setCS(HIGH);
  delay(1);
  c1101_setCS(LOW);
  delayInaccurateUs(100);
  c1101_sendStrobe(CC1101_SRES);
  delayInaccurateUs(100);
  c1101_setCS(HIGH);
}

void c1101_init(void) {
  c1101_setCS(TRUE);
  delay(10);
  c1101_reset();
  c1101_regConfigSettings();
}

bool_t c1101_detect(void) {
  uint8_t version = c1101_readStatus(CC1101_VERSION);
  return (version > 0) ? TRUE : FALSE;
}

// ============ FUNCIONES PÚBLICAS DE CONFIGURACIÓN ============

void c1101_setSidle(void) { c1101_sendStrobe(CC1101_SIDLE); }

void c1101_setModulation(uint8_t mode) {
  if (mode > 4)
    mode = 4;
  c1101.modulation_mode = mode;
  c1101_split_MDMCFG2();

  switch (mode) {
  case MOD_2FSK:
    c1101.m2MODFM = 0x00;
    c1101.frend0_val = 0x10;
    break;
  case MOD_GFSK:
    c1101.m2MODFM = 0x10;
    c1101.frend0_val = 0x10;
    break;
  case MOD_ASK_OOK:
    c1101.m2MODFM = 0x30;
    c1101.frend0_val = 0x11;
    break;
  case MOD_4FSK:
    c1101.m2MODFM = 0x40;
    c1101.frend0_val = 0x10;
    break;
  case MOD_MSK:
    c1101.m2MODFM = 0x70;
    c1101.frend0_val = 0x10;
    break;
  }

  c1101_writeReg(CC1101_MDMCFG2,
                 c1101.m2DCOFF + c1101.m2MODFM + c1101.m2MANCH + c1101.m2SYNCM);
  c1101_writeReg(CC1101_FREND0, c1101.frend0_val);
  c1101_setPA(c1101.pa_power);
}

void c1101_setFrequency(float freqMHz) {
  c1101.freq_mhz = freqMHz;
  // f_osc = 26MHz. La fórmula es: FREQ = (f_target * 2^16) / f_osc
  uint32_t freq_val = (uint32_t)(freqMHz * 65536.0 / 26.0);

  c1101_writeReg(CC1101_FREQ2, (uint8_t)((freq_val >> 16) & 0xFF));
  c1101_writeReg(CC1101_FREQ1, (uint8_t)((freq_val >> 8) & 0xFF));
  c1101_writeReg(CC1101_FREQ0, (uint8_t)(freq_val & 0xFF));

  c1101_calibrate();
}

void c1101_setRxBW(float bandwidth_khz) {
  c1101_split_MDMCFG4();
  int s1 = 3;
  int s2 = 3;
  float f = bandwidth_khz;

  for (int i = 0; i < 3; i++) {
    if (f > 101.5625) {
      f /= 2;
      s1--;
    } else {
      break;
    }
  }

  for (int i = 0; i < 3; i++) {
    if (f > 58.1) {
      f /= 1.25;
      s2--;
    } else {
      break;
    }
  }

  s1 *= 64;
  s2 *= 16;
  c1101.m4RxBw = s1 + s2;
  c1101_writeReg(CC1101_MDMCFG4, c1101.m4RxBw + c1101.m4DaRa);
}

void c1101_setDataRate(float rate_kbps) {
  c1101_split_MDMCFG4();
  float c = rate_kbps;
  uint8_t MDMCFG3 = 0;

  if (c > 1621.83)
    c = 1621.83;
  if (c < 0.0247955)
    c = 0.0247955;

  c1101.m4DaRa = 0;
  for (int i = 0; i < 20; i++) {
    if (c <= 0.0494942) {
      c = c - 0.0247955;
      c = c / 0.00009685;
      MDMCFG3 = (uint8_t)c;
      float s1 = (c - MDMCFG3) * 10;
      if (s1 >= 5)
        MDMCFG3++;
      break;
    } else {
      c1101.m4DaRa++;
      c = c / 2;
    }
  }

  c1101_writeReg(CC1101_MDMCFG4, c1101.m4RxBw + c1101.m4DaRa);
  c1101_writeReg(CC1101_MDMCFG3, MDMCFG3);
}

void c1101_setPktFormat(uint8_t format) {
  c1101_split_PKTCTRL0();
  c1101.pc0PktForm = 0;
  if (format > 3)
    format = 3;
  c1101.pc0PktForm = format * 16;
  c1101_writeReg(CC1101_PKTCTRL0, c1101.pc0WDATA + c1101.pc0PktForm +
                                      c1101.pc0CRC_EN + c1101.pc0LenConf);
}

void c1101_setSyncMode(uint8_t mode) {
  c1101_split_MDMCFG2();
  c1101.m2SYNCM = 0;
  if (mode > 7)
    mode = 7;
  c1101.m2SYNCM = mode;
  c1101_writeReg(CC1101_MDMCFG2,
                 c1101.m2DCOFF + c1101.m2MODFM + c1101.m2MANCH + c1101.m2SYNCM);
}

void c1101_setWhiteData(bool_t enable) {
  c1101_split_PKTCTRL0();
  c1101.pc0WDATA = 0;
  if (enable)
    c1101.pc0WDATA = 64;
  c1101_writeReg(CC1101_PKTCTRL0, c1101.pc0WDATA + c1101.pc0PktForm +
                                      c1101.pc0CRC_EN + c1101.pc0LenConf);
}

void c1101_setCrc(bool_t enable) {
  c1101_split_PKTCTRL0();
  c1101.pc0CRC_EN = 0;
  if (enable)
    c1101.pc0CRC_EN = 4;
  c1101_writeReg(CC1101_PKTCTRL0, c1101.pc0WDATA + c1101.pc0PktForm +
                                      c1101.pc0CRC_EN + c1101.pc0LenConf);
}

void c1101_setPower(int8_t power_dbm) { c1101_setPA(power_dbm); }

// ============ FUNCIONES PÚBLICAS DE OPERACIÓN ============

void c1101_setRxMode(void) {
  c1101_sendStrobe(CC1101_SIDLE);
  c1101_sendStrobe(CC1101_SRX);
}

void c1101_setTxMode(void) {
  c1101_sendStrobe(CC1101_SIDLE);
  c1101_sendStrobe(CC1101_STX);
}

// ============ CONFIGURACIÓN PARA JAM ============

void c1101_configureForJam(float frequency) {
  c1101_sendStrobe(CC1101_SRES);
  delay(10);

  c1101_writeReg(CC1101_IOCFG0,
                 0x0D);          // GDO0 define activación de señal portadora
  c1101_setFrequency(frequency); // Configuración de frecuencia
  c1101_writeReg(CC1101_MDMCFG2, 0x32);  // Modulación ASK
  c1101_writeReg(CC1101_PKTCTRL0, 0x32); // Modo Asíncrono Serial
  c1101_writeReg(CC1101_MCSM0, 0x18);    // Auto-calibración
  c1101_writeReg(CC1101_PATABLE, 0xC0);  // Potencia máxima

  c1101_sendStrobe(CC1101_SCAL);
  delay(10);
}
