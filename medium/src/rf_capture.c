/**
 * @file rf_capture.c
 * @brief RF capture implementation - CC1101 GPIO and raw GDO0 polling
 *
 * Adapted from branch_rf shield.c.
 * Implements GPIO control for CC1101 transceiver and raw signal capture.
 */

#include "rf_capture.h"
#include <stdio.h>

/*==================[global config]=========================================*/

RFCaptureConfig_t xCurrentRFConfig = {
    .freq_mhz = 433.92,
    .modulation_mode = MOD_ASK_OOK,
    .bandwidth_khz = 101.0,
    .delay_us = 10
};

/*==================[GPIO functions for CC1101]=============================*/

void cc1101_initGPIO(void) {
    /* Configure CS (P1_20 → GPIO0[15]) as output, default HIGH */
    Chip_SCU_PinMuxSet(CC1101_CS_SCU_PORT, CC1101_CS_SCU_PIN,
                       (SCU_MODE_INACT | SCU_MODE_FUNC0));
    Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, CC1101_CS_GPIO_PORT,
                              CC1101_CS_GPIO_PIN);
    cc1101_setCS(TRUE);

    /* Configure GDO0 (P6_4 → GPIO3[3]) as input */
    Chip_SCU_PinMuxSet(CC1101_GDO0_SCU_PORT, CC1101_GDO0_SCU_PIN,
                       (SCU_MODE_INACT | SCU_MODE_INBUFF_EN | SCU_MODE_ZIF_DIS |
                        SCU_MODE_FUNC0));
    Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, CC1101_GDO0_GPIO_PORT,
                             CC1101_GDO0_GPIO_PIN);

    /* Configure GDO2 (P6_1 → GPIO3[0]) as input
     * This reclaims P6_1 from default SD CS assignment */
    Chip_SCU_PinMuxSet(CC1101_GDO2_SCU_PORT, CC1101_GDO2_SCU_PIN,
                       (SCU_MODE_INACT | SCU_MODE_INBUFF_EN | SCU_MODE_ZIF_DIS |
                        SCU_MODE_FUNC0));
    Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, CC1101_GDO2_GPIO_PORT,
                             CC1101_GDO2_GPIO_PIN);
}

void cc1101_setCS(bool_t state) {
    Chip_GPIO_SetPinState(LPC_GPIO_PORT, CC1101_CS_GPIO_PORT,
                          CC1101_CS_GPIO_PIN, state);
}

void cc1101_setGDO0(bool_t state) {
    Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, CC1101_GDO0_GPIO_PORT,
                              CC1101_GDO0_GPIO_PIN);
    Chip_GPIO_SetPinState(LPC_GPIO_PORT, CC1101_GDO0_GPIO_PORT,
                          CC1101_GDO0_GPIO_PIN, state);
}

bool_t cc1101_getGDO0(void) {
    Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, CC1101_GDO0_GPIO_PORT,
                             CC1101_GDO0_GPIO_PIN);
    return Chip_GPIO_GetPinState(LPC_GPIO_PORT, CC1101_GDO0_GPIO_PORT,
                                 CC1101_GDO0_GPIO_PIN);
}

/*==================[RF setup]==============================================*/

void rf_setup(void) {
    cc1101_init();
    cc1101_setSidle();
    cc1101_setModulation(xCurrentRFConfig.modulation_mode);
    cc1101_setFrequency(xCurrentRFConfig.freq_mhz);
    cc1101_setRxBW(xCurrentRFConfig.bandwidth_khz);
    cc1101_setDataRate(5.0);
    cc1101_setPktFormat(3);     /* Async mode */
    cc1101_setSyncMode(0);      /* No sync */
    cc1101_setWhiteData(FALSE); /* No whitening */
    cc1101_setCrc(FALSE);       /* No CRC */
}

/*==================[raw capture]===========================================*/

bool_t rf_capture_raw(uint8_t *buffer, int delay_us_val) {
    if (buffer == NULL || delay_us_val <= 0) {
        return FALSE;
    }

    /* Configure async mode and enter RX */
    cc1101_setPktFormat(3);
    cc1101_setRxMode();

    /* Wait for signal (GDO0 HIGH) with 3-second timeout */
    uint32_t timeout = 0;
    while (cc1101_getGDO0() == FALSE) {
        delayInaccurateUs(delay_us_val);
        timeout += delay_us_val;
        if (timeout > 3000000) {
            return FALSE;
        }
    }

    /* Signal detected — sample GDO0 into buffer */
    for (int i = 0; i < RF_RECORDING_BUFFER_SIZE; i++) {
        uint8_t received_byte = 0;
        for (int j = 7; j >= 0; j--) {
            if (cc1101_getGDO0()) {
                received_byte |= (1 << j);
            }
            delayInaccurateUs(delay_us_val);
        }
        buffer[i] = received_byte;
    }

    return TRUE;
}

/*==================[end of file]===========================================*/
