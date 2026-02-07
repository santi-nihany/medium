/**
 * @file signal_storage.h
 * @brief Storage module for managing signal files on microSD
 * 
 * This module provides file system operations for storing and retrieving
 * signal data. Only this module should have write access to the SD card.
 */

#ifndef _SIGNAL_STORAGE_H_
#define _SIGNAL_STORAGE_H_

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "signal_capture.h"

/*==================[macros and definitions]=================================*/

#define MAX_FILENAME_SIZE    32
#define MAX_FILE_COUNT       100

/*==================[types]==================================================*/

/**
 * @brief File info structure
 */
typedef struct {
    char filename[MAX_FILENAME_SIZE];
    uint32_t timestamp;
    uint32_t file_size;
    uint8_t mode;  /* IR or RF */
} SignalFileInfo_t;

/*==================[external functions]=====================================*/

/**
 * @brief Storage Task
 * Handles writing signal packets to microSD and file management
 * @param pvParameters Task parameters (unused)
 */
void vStorage_Task(void *pvParameters);

/**
 * @brief Enqueue a signal packet for storage
 * Ownership of packet transfers to storage task on success.
 * @param packet Signal packet to save (caller must NOT free on success)
 * @return pdPASS on success, pdFAIL otherwise
 */
BaseType_t Storage_SaveSignal(SignalPacket_t *packet);

/**
 * @brief Load a signal file
 * @param filename File to load
 * @param packet Output packet
 * @return pdPASS on success, pdFAIL otherwise
 */
BaseType_t Storage_LoadSignal(const char *filename, SignalPacket_t **packet);

/**
 * @brief Delete a signal file
 * @param filename File to delete
 * @return pdPASS on success, pdFAIL otherwise
 */
BaseType_t Storage_DeleteSignal(const char *filename);

/**
 * @brief List all signal files
 * @param file_list Output array of file info
 * @param max_count Maximum number of files to list
 * @return Number of files found
 */
uint32_t Storage_ListFiles(SignalFileInfo_t *file_list, uint32_t max_count);

/**
 * @brief Get storage statistics
 * @param free_space Output free space in bytes
 * @param total_space Output total space in bytes
 * @return pdPASS on success, pdFAIL otherwise
 */
BaseType_t Storage_GetStats(uint32_t *free_space, uint32_t *total_space);

/*==================[end of file]============================================*/

#endif /* _SIGNAL_STORAGE_H_ */

