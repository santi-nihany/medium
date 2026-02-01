# Médium Device Firmware - Claude Rules

## Project Overview
IR/RF signal capture, storage, and replay device using FreeRTOS on LPC microcontroller (EDU-CIAA).

## Architecture
- **Event-Driven/Time-Driven hybrid** on preemptive FreeRTOS v10
- **ISRs** capture signals → **StreamBuffers** → **Capture Tasks** → **StorageQueue** → **Storage Task** → microSD
- **UI Task** handles FSM-based user interface via command queue

## Critical FreeRTOS Rules

### Task Priorities (DO NOT CHANGE without reason)
- 4: SignalCapture tasks (time-critical)
- 3: Storage task (SD I/O)
- 2: Replay task
- 1: UI and Housekeeping (low priority)

### ISR Rules
1. ALWAYS use `*FromISR()` variants: `xQueueSendFromISR`, `xStreamBufferSendFromISR`
2. ALWAYS track `xHigherPriorityTaskWoken` and call `portYIELD_FROM_ISR()` at end
3. NEVER use blocking calls in ISRs
4. Keep ISRs minimal - defer work to tasks

### Queue/Buffer Patterns
- **StorageQueue**: passes `SignalPacket_t*` pointers (ownership transfers to consumer)
- **UICommandQueue**: passes `UICommand_t` by value
- **StreamBuffers**: raw `uint32_t` samples from ISRs

### Memory Management
- Use `pvPortMalloc/vPortFree` (NOT malloc/free)
- Consumer of queue pointer MUST free after processing
- 24KB total heap - monitor with `xPortGetFreeHeapSize()`

## Code Style

### Task Implementation Pattern
```c
void vMyTask(void *pvParameters)
{
    printf("[Module] Task started\r\n");

    for (;;) {
        // Block on queue/semaphore/timer - ALWAYS use blocking, not polling
        if (xQueueReceive(myQueue, &item, portMAX_DELAY) == pdPASS) {
            // Process item
        }
    }
}
```

### Printf Prefix Convention
- `[Storage]` for storage module
- `[Test]` for test tasks
- `[UI]` for UI controller
- Module name in brackets for all debug output

## Testing Strategy
1. Enable ONE task at a time with printf/vTaskDelay to verify scheduling
2. Use `test_storage.c` to inject synthetic packets without hardware
3. Monitor heap with Housekeeping task

## File Structure
- `src/` - Source files (.c)
- `inc/` - Headers (.h)
- `debug/` - Test plans and decision docs

## Hardware TODOs (marked in code)
- Timer capture for IR/RF timestamps
- GPIO for buttons and signal levels
- LCD display driver
- RTC for real timestamps
- Watchdog

## FatFS Integration
- `diskTickHook()` MUST be called every 10ms (via tickCallbackSet)
- Use `xStorageMutex` for exclusive SD access
- File path format: `SDC:/signals/signal_IR_000001.sig`

## Common Pitfalls to Avoid
1. Don't use `xTaskGetTickCountFromISR()` in task context - use `xTaskGetTickCount()`
2. Don't forget to `vPortFree()` received queue packets
3. Don't access SD without taking `xStorageMutex`
4. Don't use `printf` in time-critical ISRs
5. Empty `for(;;)` loops without blocking will starve other tasks

## Build
```bash
# From project root
make
```
Config in `config.mk` - FreeRTOS heap type 4, FatFS enabled
