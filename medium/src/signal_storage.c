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

/* Standard C */
#include <stdio.h>
#include <string.h>

/* FatFS includes */
#include "ff.h"
#include "fssdc.h"
#include "sapi.h"

/*==================[macros and definitions]=================================*/

/* Directory for signal files */
#define SIGNAL_DIR "SDC:/signals"

/* File extension - Using .sig for signal files */
#define FILE_EXTENSION ".sig"

/*==================[internal data]==========================================*/

static FATFS fs;           // FatFs work area needed for each volume
static FIL fil;            // File object
static BaseType_t sd_mounted = pdFALSE;
static SemaphoreHandle_t xStorageInUse = NULL;

/*==================[external data]==========================================*/

extern QueueHandle_t xStorageQueue;
extern SemaphoreHandle_t xStorageMutex;

/*==================[internal functions]=====================================*/

/**
 * @brief Initialize SD card and mount filesystem
 * @return pdPASS if successful, pdFAIL otherwise
 */
static BaseType_t MountSD(void)
{
    FRESULT fr;
    
    if (sd_mounted) {
        return pdPASS; // Already mounted
    }
    
    printf("[Storage] Initializing SD card...\r\n");
    
    // Initialize SD card SPI driver
    FSSDC_InitSPI();
    
    // Mount the SD card filesystem
    fr = f_mount(&fs, "SDC:", 1); // 1 = force mount
    
    if (fr == FR_OK) {
        sd_mounted = pdTRUE;
        printf("[Storage] SD card mounted successfully\r\n");
        
        // Create signals directory if it doesn't exist
        f_mkdir("SDC:/signals");
        
        return pdPASS;
    } else {
        printf("[Storage] ERROR: Failed to mount SD card (FRESULT=%d)\r\n", fr);
        sd_mounted = pdFALSE;
        return pdFAIL;
    }
}

/**
 * @brief Generate filename with timestamp
 * @param filename Output buffer for filename
 * @param mode IR or RF mode
 * @return pdPASS on success
 */
static BaseType_t GenerateFilename(char *filename, uint8_t mode)
{
    static uint32_t file_counter = 0;
    
    if (filename == NULL) {
        return pdFAIL;
    }
    
    // Format: signals/signal_IR_000001.sig or signals/signal_RF_000001.sig
    sprintf(filename, "%s/signal_%s_%06d%s", 
            SIGNAL_DIR, 
            (mode == SIGNAL_MODE_IR) ? "IR" : "RF", 
            file_counter++,
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
    UINT total_bytes = 0;
    
    printf("[Storage] Storage Task started\r\n");

    /* Note: xStorageMutex is created in main.c, no need to create another */

    // Mount SD card
    if (MountSD() != pdPASS) {
        printf("[Storage] ERROR: Cannot mount SD card, task will retry\r\n");
    }
    
    // Main task loop
    for (;;) {
        // Wait for packets from capture tasks
        if (xQueueReceive(xStorageQueue, &packet, portMAX_DELAY) == pdPASS) {
            
            // Take storage mutex (only one task can access SD)
            if (xSemaphoreTake(xStorageMutex, portMAX_DELAY) == pdPASS) {
                
                // Check if SD is mounted
                if (!sd_mounted) {
                    if (MountSD() != pdPASS) {
                        printf("[Storage] ERROR: Cannot write, SD not mounted\r\n");
                        vPortFree(packet);
                        xSemaphoreGive(xStorageMutex);
                        continue;
                    }
                }
                
                // Generate filename
                if (GenerateFilename(filename, packet->mode) == pdPASS) {
                    
                    printf("[Storage] Saving signal to: %s\r\n", filename);
                    
                    // Open file for writing (create new file)
                    fr = f_open(&fil, filename, FA_WRITE | FA_CREATE_ALWAYS);
                    
                    if (fr == FR_OK) {
                        
                        // Write file header (simple version)
                        char header[64];
                        int n = sprintf(header, "MED1;VER1;TS=%lu;MODE=%d;SAMPLES=%lu\r\n",
                                        packet->timestamp_ms,
                                        packet->mode,
                                        packet->sample_count);
                        f_write(&fil, header, n, &bytes_written);
                        total_bytes = bytes_written;
                        
                        // Write signal data
                        uint32_t data_size = sizeof(uint32_t) * packet->sample_count;
                        fr = f_write(&fil, packet->data, data_size, &bytes_written);
                        total_bytes += bytes_written;
                        
                        // Close file
                        f_close(&fil);
                        
                        if (fr == FR_OK) {
                            printf("[Storage] Wrote %d bytes to %s\r\n", total_bytes, filename);
                            gpioWrite(LEDG, ON);
                            vTaskDelay(pdMS_TO_TICKS(100));
                            gpioWrite(LEDG, OFF);
                        } else {
                            printf("[Storage] ERROR: Write failed (FRESULT=%d)\r\n", fr);
                            gpioWrite(LEDR, ON);
                        }
                        
                    } else {
                        printf("[Storage] ERROR: Cannot open file (FRESULT=%d)\r\n", fr);
                        gpioWrite(LEDR, ON);
                    }
                    
                } else {
                    printf("[Storage] ERROR: Cannot generate filename\r\n");
                    gpioWrite(LEDR, ON);
                }
                
                // Free packet memory
                vPortFree(packet);
                
                // Release mutex
                xSemaphoreGive(xStorageMutex);
                
            } else {
                // Mutex timeout or error
                printf("[Storage] WARNING: Cannot take mutex\r\n");
                vPortFree(packet); // Free packet to avoid memory leak
            }
        }
        
        // Small delay to prevent CPU spinning
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

BaseType_t Storage_Init(void)
{
    printf("[Storage] Initializing storage module...\r\n");
    
    // Try to mount SD card
    if (MountSD() == pdPASS) {
        printf("[Storage] Initialization successful\r\n");
        return pdPASS;
    } else {
        printf("[Storage] Initialization failed, will retry later\r\n");
        return pdFAIL;
    }
}

BaseType_t Storage_SaveSignal(SignalPacket_t *packet, const char *filename)
{
    // This function is called from UI context
    // We should send to queue instead of direct access
    
    if (packet == NULL || filename == NULL) {
        return pdFAIL;
    }
    
    // Send to storage queue
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
    
    if (filename == NULL || packet == NULL) {
        return pdFAIL;
    }
    
    // Check if SD is mounted
    if (!sd_mounted) {
        printf("[Storage] ERROR: Cannot load, SD not mounted\r\n");
        return pdFAIL;
    }
    
    // Open file for reading
    fr = f_open(&fil, filename, FA_READ);
    if (fr != FR_OK) {
        printf("[Storage] ERROR: Cannot open file for reading (FRESULT=%d)\r\n", fr);
        return pdFAIL;
    }
    
    // TODO: Parse header to get packet size
    
    // Read data (placeholder)
    // This is a simplified version, needs proper implementation
    
    f_close(&fil);
    
    return pdPASS;
}

BaseType_t Storage_DeleteSignal(const char *filename)
{
    FRESULT fr;
    
    if (filename == NULL) {
        return pdFAIL;
    }
    
    // Check if SD is mounted
    if (!sd_mounted) {
        printf("[Storage] ERROR: Cannot delete, SD not mounted\r\n");
        return pdFAIL;
    }
    
    // Delete file
    fr = f_unlink(filename);
    if (fr == FR_OK) {
        printf("[Storage] Deleted: %s\r\n", filename);
        return pdPASS;
    } else {
        printf("[Storage] ERROR: Cannot delete file (FRESULT=%d)\r\n", fr);
        return pdFAIL;
    }
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
    
    // Check if SD is mounted
    if (!sd_mounted) {
        printf("[Storage] ERROR: Cannot list, SD not mounted\r\n");
        return 0;
    }
    
    // Open signals directory
    fr = f_opendir(&dir, SIGNAL_DIR);
    if (fr != FR_OK) {
        printf("[Storage] ERROR: Cannot open directory (FRESULT=%d)\r\n", fr);
        return 0;
    }
    
    // Read directory entries
    while (count < max_count) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) {
            break; // End of directory or error
        }
        
        // Check if it's a .sig file
        if (!(fno.fattrib & AM_DIR) && strstr(fno.fname, FILE_EXTENSION) != NULL) {
            strncpy(file_list[count].filename, fno.fname, MAX_FILENAME_SIZE - 1);
            file_list[count].file_size = fno.fsize;
            file_list[count].timestamp = 0; // TODO: Parse from filename or header
            count++;
        }
    }
    
    f_closedir(&dir);
    
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
    
    // Check if SD is mounted
    if (!sd_mounted) {
        return pdFAIL;
    }
    
    // Get filesystem info
    fr = f_getfree("SDC:", &fre_clust, &fs_ptr);
    if (fr != FR_OK) {
        return pdFAIL;
    }
    
    // Calculate free and total space
    tot_sect = (fs_ptr->n_fatent - 2) * fs_ptr->csize;
    fre_sect = fre_clust * fs_ptr->csize;
    
    // Convert to bytes (assuming 512 bytes per sector)
    *total_space = tot_sect * 512;
    *free_space = fre_sect * 512;
    
    return pdPASS;
}
