/**
 * @file signal_storage.c
 * @brief Signal storage implementation with FatFS integration
 *
 * Based on examples from CIAA firmware:
 * - examples/c/sapi/spi/sd_card/fatfs_write_file
 * - examples/c/sapi/spi/sd_card/fatfs_log_time_stamp
 * - examples/c/sapi/spi/sd_card/fatfs_list_files
 */

#include "signal_storage.h"
#include "rf_capture.h"

/* Standard C */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* FatFS includes */
#include "ff.h"
#include "fssdc.h"

/* FreeRTOS for heap monitoring */
#include "FreeRTOS.h"
#include "task.h"

/*==================[macros and definitions]=================================*/

/* Directory for signal files */
#define SIGNAL_DIR "SDC:/signals"

/* File extension - Using .sig for signal files */
#define FILE_EXTENSION ".sig"

/*==================[internal data]==========================================*/

static FATFS fs;           // FatFs work area needed for each volume
static FIL fil;            // File object (only use from vStorage_Task context!)
static BaseType_t sd_mounted = pdFALSE;

/*==================[external data]==========================================*/

extern QueueHandle_t xStorageQueue;
extern SemaphoreHandle_t xSPIMutex;

/*==================[internal functions]=====================================*/

/**
 * @brief Initialize SD card and mount filesystem
 * @return pdPASS if successful, pdFAIL otherwise
 */
static BaseType_t MountSD(void)
{
    FRESULT fr;
    static BaseType_t spi_initialized = pdFALSE;

    if (sd_mounted) {
        return pdPASS;
    }

    printf("[Storage] Initializing SD card...\r\n");

    // Initialize SD card SPI driver (only once)
    if (!spi_initialized) {
        FSSDC_SetFastClock( 14000000 );  // 14 MHz (default 15 MHz falla)
        FSSDC_InitSPI();
        spi_initialized = pdTRUE;

        // Wait for disk timer to run a few times (important for card init)
        printf("[Storage] Waiting for card to stabilize...\r\n");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Register filesystem (delayed mount - 0, matching reference pattern)
    fr = f_mount(&fs, "SDC:", 0);
    if (fr != FR_OK) {
        printf("[Storage] ERROR: f_mount failed (FRESULT=%d)\r\n", fr);
        return pdFAIL;
    }

    // Force mount by accessing the volume (triggers actual card init)
    DWORD fre_clust;
    FATFS *fs_ptr;
    fr = f_getfree("SDC:", &fre_clust, &fs_ptr);

    if (fr == FR_OK) {
        sd_mounted = pdTRUE;
        printf("[Storage] SD card mounted successfully\r\n");

        // Create signals directory if it doesn't exist
        f_mkdir(SIGNAL_DIR);

        return pdPASS;
    }

    // Mount failed - unregister filesystem before next retry
    printf("[Storage] ERROR: Failed to access SD card (FRESULT=%d)\r\n", fr);
    f_mount(NULL, "SDC:", 0);
    sd_mounted = pdFALSE;
    return pdFAIL;
}

/**
 * @brief Generate filename with sequential counter
 * @param filename Output buffer (must be at least 64 bytes)
 * @param mode SIGNAL_MODE_IR or SIGNAL_MODE_RF
 * @return pdPASS on success
 */
static BaseType_t GenerateFilename(char *filename, uint8_t mode)
{
    static uint32_t file_counter = 0;

    if (filename == NULL) {
        return pdFAIL;
    }

    sprintf(filename, "%s/signal_%s_%06lu%s",
            SIGNAL_DIR,
            (mode == SIGNAL_MODE_IR) ? "IR" : "RF",
            (unsigned long)file_counter++,
            FILE_EXTENSION);

    return pdPASS;
}

/*==================[external functions]=====================================*/

void vStorage_Task(void *pvParameters)
{
    SignalPacket_t *packet = NULL;
    char filename[64];
    FRESULT fr;
    UINT bytes_written;
    uint32_t total_bytes;
    static uint32_t packets_saved = 0;

    printf("[Storage] Storage Task started (SD write enabled)\r\n");

    /* Mount SD card */
    if (MountSD() != pdPASS) {
        printf("[Storage] WARNING: SD mount failed, will retry on first write\r\n");
    }

    for (;;) {
        if (xQueueReceive(xStorageQueue, &packet, portMAX_DELAY) != pdPASS) {
            continue;
        }

        printf("[Storage] === PACKET #%lu ===\r\n", (unsigned long)(packets_saved + 1));
        printf("[Storage]   Mode: %s, Samples: %lu\r\n",
               (packet->mode == SIGNAL_MODE_IR) ? "IR" : "RF",
               (unsigned long)packet->sample_count);

        /* Try to mount SD if not mounted */
        if (!sd_mounted) {
            printf("[Storage] Attempting SD mount...\r\n");
            if (MountSD() != pdPASS) {
                printf("[Storage] ERROR: SD mount failed, packet discarded\r\n");
                vPortFree(packet);
                continue;
            }
        }

        /* Take mutex for exclusive SD access */
        if (xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(5000)) != pdPASS) {
            printf("[Storage] ERROR: Cannot take mutex (timeout)\r\n");
            vPortFree(packet);
            printf("[Storage] Heap: %lu bytes\r\n",
                   (unsigned long)xPortGetFreeHeapSize());
            printf("[Storage] ====================\r\n");
            continue;
        }

        /* Generate filename */
        if (GenerateFilename(filename, packet->mode) != pdPASS) {
            printf("[Storage] ERROR: Cannot generate filename\r\n");
            xSemaphoreGive(xSPIMutex);
            vPortFree(packet);
            continue;
        }

        printf("[Storage] Writing to: %s\r\n", filename);

        /* Open file for writing (reference: fatfs_write_file pattern) */
        fr = f_open(&fil, filename, FA_WRITE | FA_CREATE_ALWAYS);
        if (fr != FR_OK) {
            printf("[Storage] ERROR: Cannot open file (FRESULT=%d)\r\n", fr);
            xSemaphoreGive(xSPIMutex);
            vPortFree(packet);
            continue;
        }

        /* Write header and data — format depends on signal mode */
        total_bytes = 0;

        if (packet->mode == SIGNAL_MODE_RF) {
            /* RF header: CC1101_CAPTURE format with config metadata */
            char header[200];
            int header_len = sprintf(header,
                "CC1101_CAPTURE;VER1\r\n"
                "FREQ_MHZ=%.2f\r\n"
                "MODULATION=%d\r\n"
                "BANDWIDTH_KHZ=%.2f\r\n"
                "DELAY_US=%lu\r\n"
                "DATA_LENGTH=%lu\r\n"
                "---DATA_START---\r\n",
                (double)xCurrentRFConfig.freq_mhz,
                (int)xCurrentRFConfig.modulation_mode,
                (double)xCurrentRFConfig.bandwidth_khz,
                (unsigned long)xCurrentRFConfig.delay_us,
                (unsigned long)packet->sample_count);

            fr = f_write(&fil, header, header_len, &bytes_written);
            if (fr != FR_OK || bytes_written != (UINT)header_len) {
                printf("[Storage] ERROR: RF header write failed (fr=%d)\r\n", fr);
                f_close(&fil);
                xSemaphoreGive(xSPIMutex);
                vPortFree(packet);
                continue;
            }
            total_bytes += bytes_written;

            /* RF data: raw bytes (1 byte per sample) */
            UINT data_size = packet->sample_count;
            fr = f_write(&fil, packet->data, data_size, &bytes_written);
            if (fr != FR_OK || bytes_written != data_size) {
                printf("[Storage] ERROR: RF data write failed (fr=%d)\r\n", fr);
                f_close(&fil);
                xSemaphoreGive(xSPIMutex);
                vPortFree(packet);
                continue;
            }
            total_bytes += bytes_written;
        } else {
            /* IR header: MED1;VER1 format */
            char header[80];
            int header_len = sprintf(header,
                "MED1;VER1;TS=%lu;MODE=%d;SAMPLES=%lu\r\n",
                (unsigned long)packet->timestamp_ms,
                packet->mode,
                (unsigned long)packet->sample_count);

            fr = f_write(&fil, header, header_len, &bytes_written);
            if (fr != FR_OK || bytes_written != (UINT)header_len) {
                printf("[Storage] ERROR: IR header write failed (fr=%d)\r\n", fr);
                f_close(&fil);
                xSemaphoreGive(xSPIMutex);
                vPortFree(packet);
                continue;
            }
            total_bytes += bytes_written;

            /* IR data: uint32_t samples */
            UINT data_size = packet->sample_count * sizeof(uint32_t);
            fr = f_write(&fil, packet->data, data_size, &bytes_written);
            if (fr != FR_OK || bytes_written != data_size) {
                printf("[Storage] ERROR: IR data write failed (fr=%d)\r\n", fr);
                f_close(&fil);
                xSemaphoreGive(xSPIMutex);
                vPortFree(packet);
                continue;
            }
            total_bytes += bytes_written;
        }

        /* Close file */
        f_close(&fil);

        packets_saved++;
        printf("[Storage] SUCCESS: Wrote %lu bytes (total saved: %lu)\r\n",
               (unsigned long)total_bytes, (unsigned long)packets_saved);

        /* Release mutex */
        xSemaphoreGive(xSPIMutex);

        /* Free packet memory */
        vPortFree(packet);
        printf("[Storage] Heap: %lu bytes\r\n",
               (unsigned long)xPortGetFreeHeapSize());
        printf("[Storage] ====================\r\n");
    }
}

BaseType_t Storage_SaveSignal(SignalPacket_t *packet)
{
    if (packet == NULL) {
        return pdFAIL;
    }

    if (xStorageQueue != NULL) {
        if (xQueueSend(xStorageQueue, &packet, pdMS_TO_TICKS(1000)) == pdPASS) {
            return pdPASS;
        }
    }

    return pdFAIL;
}

BaseType_t Storage_LoadSignal(const char *filename, SignalPacket_t **packet)
{
    FRESULT fr;
    UINT bytes_read;
    char filepath[64];
    char header_buf[128];

    if (filename == NULL || packet == NULL) {
        return pdFAIL;
    }

    if (!sd_mounted) {
        printf("[Storage] ERROR: Cannot load, SD not mounted\r\n");
        return pdFAIL;
    }

    /* Build full path: SIGNAL_DIR/filename */
    snprintf(filepath, sizeof(filepath), "%s/%s", SIGNAL_DIR, filename);

    if (xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(5000)) != pdPASS) {
        printf("[Storage] ERROR: Cannot take mutex for load\r\n");
        return pdFAIL;
    }

    fr = f_open(&fil, filepath, FA_READ);
    if (fr != FR_OK) {
        printf("[Storage] ERROR: Cannot open %s (FRESULT=%d)\r\n", filepath, fr);
        xSemaphoreGive(xSPIMutex);
        return pdFAIL;
    }

    /* Read header line (ASCII, terminated by \n) */
    UINT i = 0;
    uint8_t ch;
    while (i < sizeof(header_buf) - 1) {
        fr = f_read(&fil, &ch, 1, &bytes_read);
        if (fr != FR_OK || bytes_read == 0) break;
        header_buf[i++] = (char)ch;
        if (ch == '\n') break;
    }
    header_buf[i] = '\0';

    /* Parse header: MED1;VER1;TS=<ts>;MODE=<m>;SAMPLES=<n>\r\n */
    uint32_t ts = 0;
    int mode = 0;
    uint32_t sample_count = 0;

    if (strncmp(header_buf, "MED1;VER1;", 10) != 0) {
        printf("[Storage] ERROR: Unknown header format: %.20s...\r\n", header_buf);
        f_close(&fil);
        xSemaphoreGive(xSPIMutex);
        return pdFAIL;
    }

    /* Parse fields from header */
    char *p = header_buf + 10;  /* skip "MED1;VER1;" */
    char *token;

    token = strstr(p, "TS=");
    if (token) ts = strtoul(token + 3, NULL, 10);

    token = strstr(p, "MODE=");
    if (token) mode = atoi(token + 5);

    token = strstr(p, "SAMPLES=");
    if (token) sample_count = strtoul(token + 8, NULL, 10);

    if (sample_count == 0 || sample_count > 1024) {
        printf("[Storage] ERROR: Invalid sample count: %lu\r\n",
               (unsigned long)sample_count);
        f_close(&fil);
        xSemaphoreGive(xSPIMutex);
        return pdFAIL;
    }

    printf("[Storage] Loading: mode=%d, samples=%lu, ts=%lu\r\n",
           mode, (unsigned long)sample_count, (unsigned long)ts);

    /* Allocate packet */
    size_t data_size = sample_count * sizeof(uint32_t);
    SignalPacket_t *pkt = pvPortMalloc(sizeof(SignalPacket_t) + data_size);
    if (pkt == NULL) {
        printf("[Storage] ERROR: Failed to allocate packet for load\r\n");
        f_close(&fil);
        xSemaphoreGive(xSPIMutex);
        return pdFAIL;
    }

    pkt->mode = (uint8_t)mode;
    pkt->timestamp_ms = ts;
    pkt->sample_count = sample_count;

    /* Read binary sample data */
    fr = f_read(&fil, pkt->data, data_size, &bytes_read);
    if (fr != FR_OK || bytes_read != data_size) {
        printf("[Storage] ERROR: Data read failed (fr=%d, read=%u, expected=%u)\r\n",
               fr, (unsigned)bytes_read, (unsigned)data_size);
        vPortFree(pkt);
        f_close(&fil);
        xSemaphoreGive(xSPIMutex);
        return pdFAIL;
    }

    f_close(&fil);
    xSemaphoreGive(xSPIMutex);

    *packet = pkt;
    printf("[Storage] Loaded %s: %lu samples, %lu bytes\r\n",
           filename, (unsigned long)sample_count, (unsigned long)data_size);
    return pdPASS;
}

BaseType_t Storage_DeleteSignal(const char *filename)
{
    FRESULT fr;
    BaseType_t result = pdFAIL;

    if (filename == NULL) {
        return pdFAIL;
    }

    if (!sd_mounted) {
        printf("[Storage] ERROR: Cannot delete, SD not mounted\r\n");
        return pdFAIL;
    }

    if (xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(5000)) != pdPASS) {
        printf("[Storage] ERROR: Cannot take mutex for delete\r\n");
        return pdFAIL;
    }

    fr = f_unlink(filename);
    if (fr == FR_OK) {
        printf("[Storage] Deleted: %s\r\n", filename);
        result = pdPASS;
    } else {
        printf("[Storage] ERROR: Cannot delete file (FRESULT=%d)\r\n", fr);
    }

    xSemaphoreGive(xSPIMutex);
    return result;
}

uint32_t Storage_ListFiles(SignalFileInfo_t *file_list, uint32_t max_count)
{
    FRESULT fr;
    DIR dir;
    FILINFO fno;
    uint32_t count = 0;

    if (file_list == NULL || max_count == 0) {
        return 0;
    }

    if (!sd_mounted) {
        printf("[Storage] ERROR: Cannot list, SD not mounted\r\n");
        return 0;
    }

    if (xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(5000)) != pdPASS) {
        printf("[Storage] ERROR: Cannot take mutex for list\r\n");
        return 0;
    }

    fr = f_opendir(&dir, SIGNAL_DIR);
    if (fr != FR_OK) {
        printf("[Storage] ERROR: Cannot open directory (FRESULT=%d)\r\n", fr);
        xSemaphoreGive(xSPIMutex);
        return 0;
    }

    while (count < max_count) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) {
            break;
        }

        if (!(fno.fattrib & AM_DIR) && strstr(fno.fname, FILE_EXTENSION) != NULL) {
            strncpy(file_list[count].filename, fno.fname, MAX_FILENAME_SIZE - 1);
            file_list[count].file_size = fno.fsize;
            file_list[count].timestamp = 0; // TODO: Parse from header
            count++;
        }
    }

    f_closedir(&dir);
    xSemaphoreGive(xSPIMutex);

    return count;
}

BaseType_t Storage_GetStats(uint32_t *free_space, uint32_t *total_space)
{
    FATFS *fs_ptr;
    DWORD fre_clust, fre_sect, tot_sect;
    FRESULT fr;

    if (free_space == NULL || total_space == NULL) {
        return pdFAIL;
    }

    if (!sd_mounted) {
        return pdFAIL;
    }

    if (xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(5000)) != pdPASS) {
        return pdFAIL;
    }

    fr = f_getfree("SDC:", &fre_clust, &fs_ptr);
    if (fr != FR_OK) {
        xSemaphoreGive(xSPIMutex);
        return pdFAIL;
    }

    tot_sect = (fs_ptr->n_fatent - 2) * fs_ptr->csize;
    fre_sect = fre_clust * fs_ptr->csize;

    *total_space = tot_sect * 512;
    *free_space = fre_sect * 512;

    xSemaphoreGive(xSPIMutex);
    return pdPASS;
}
