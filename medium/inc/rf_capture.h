/**
 * @file rf_capture.h
 * @brief RF capture module - CC1101 GPIO, pin mappings, and capture API
 *
 * Adapted from branch_rf shield.h/shield.c.
 * Provides GPIO initialization for CC1101 transceiver and raw RF capture
 * using GDO0 polling.
 */

#ifndef _RF_CAPTURE_H_
#define _RF_CAPTURE_H_

#include "chip.h"
#include "sapi.h"
#include "cc1101.h"

/*==================[CC1101 pin mappings]===================================*/

/* CS pin: P1_20 → GPIO0[15] */
#define CC1101_CS_SCU_PORT    1
#define CC1101_CS_SCU_PIN     20
#define CC1101_CS_GPIO_PORT   0
#define CC1101_CS_GPIO_PIN    15

/* GDO0 pin: P6_4 → GPIO3[3] (signal I/O for capture) */
#define CC1101_GDO0_SCU_PORT  6
#define CC1101_GDO0_SCU_PIN   4
#define CC1101_GDO0_GPIO_PORT 3
#define CC1101_GDO0_GPIO_PIN  3

/* GDO2 pin: P6_1 → GPIO3[0] (reclaimed from default SD CS) */
#define CC1101_GDO2_SCU_PORT  6
#define CC1101_GDO2_SCU_PIN   1
#define CC1101_GDO2_GPIO_PORT 3
#define CC1101_GDO2_GPIO_PIN  0

/*==================[capture constants]=====================================*/

#define RF_RECORDING_BUFFER_SIZE  4096

/*==================[types]=================================================*/

/**
 * @brief RF capture configuration (set by UI before capture)
 */
typedef struct {
    float freq_mhz;
    uint8_t modulation_mode;
    float bandwidth_khz;
    uint32_t delay_us;
} RFCaptureConfig_t;

/*==================[external data]=========================================*/

extern RFCaptureConfig_t xCurrentRFConfig;

/*==================[functions]=============================================*/

/**
 * @brief Initialize CC1101 GPIO pins (CS, GDO0, GDO2)
 * Must be called AFTER spiConfig(SPI0) and SD CS remap
 */
void cc1101_initGPIO(void);

/**
 * @brief Full CC1101 setup for async RX mode
 * Configures modulation, frequency, bandwidth, packet format
 */
void rf_setup(void);

/**
 * @brief Perform raw RF capture via GDO0 polling
 *
 * Waits for signal on GDO0, then samples into provided buffer.
 * Blocks for duration of capture (~300ms at default settings).
 *
 * @param buffer Output buffer (must be RF_RECORDING_BUFFER_SIZE bytes)
 * @param delay_us Sampling interval in microseconds
 * @return TRUE if signal captured, FALSE on timeout
 */
bool_t rf_capture_raw(uint8_t *buffer, int delay_us);

/*==================[end of file]===========================================*/

#endif /* _RF_CAPTURE_H_ */
