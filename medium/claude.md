# Médium Device Firmware - Claude Rules

## Project Overview
IR/RF signal capture, storage, and replay device using FreeRTOS on LPC microcontroller (EDU-CIAA).

## Architecture
- **Polling-based capture** triggered by UI via `xTaskNotifyGive()`
- **IR capture**: `modulo_ir_capture()` polls GPIO7 at 30us intervals
- **RF capture**: `rf_capture_raw()` polls CC1101 GDO0 pin
- **Capture Tasks** → **StorageQueue** → **Storage Task** → microSD
- **UI Task** handles FSM-based user interface with SH1106 OLED + 4 buttons

## Critical FreeRTOS Rules

### Task Priorities (DO NOT CHANGE without reason)
- 4: SignalCapture tasks (time-critical)
- 3: Storage task (SD I/O)
- 2: Replay task
- 1: UI and Housekeeping (low priority)

### Queue/Buffer Patterns
- **StorageQueue**: passes `SignalPacket_t*` pointers (ownership transfers to consumer)
- **UICommandQueue**: passes `UICommand_t` by value

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
        // Block on queue/semaphore/notification - ALWAYS use blocking, not polling
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // Process
    }
}
```

### Printf Prefix Convention
- `[CaptureIR]` / `[CaptureRF]` for capture tasks
- `[Storage]` for storage module
- `[UI]` for UI controller
- `[Housekeeping]` for housekeeping
- Module name in brackets for all debug output

## File Structure
- `src/` - Source files (.c)
- `inc/` - Headers (.h)
- `debug/` - Test plans and decision docs

## SPI0 Bus Sharing
- SD card CS: GPIO3[12] (P7_4) — CC1101 CS: GPIO0[15] (P1_20)
- `xSPIMutex` serializes ALL SPI0 access (SD + CC1101)
- During RF GDO0 polling capture, mutex is released (GPIO-only, no SPI)
- Init order: `spiConfig(SPI0)` → SD CS remap (P7_4) → `cc1101_initGPIO()` (reclaims P6_1 for GDO2)

## FatFS Integration
- **IMPORTANT**: `tickCallbackSet()` does NOT work with FreeRTOS!
- Use `xDiskTimer` (FreeRTOS software timer, 10ms) to call `disk_timerproc()`
- Use `xSPIMutex` for exclusive SD access
- Wait 500ms after `FSSDC_InitSPI()` before mounting (let timer stabilize card)
- File path format: `SDC:/signals/signal_IR_000001.sig`
- IR file format: `MED1;VER1;TS=...;MODE=0;SAMPLES=...\r\n` + binary uint32_t samples
- RF file format: `CC1101_CAPTURE;VER1\r\n` + metadata lines + `---DATA_START---\r\n` + raw bytes

## Common Pitfalls to Avoid
1. Don't forget to `vPortFree()` received queue packets
2. Don't access SD without taking `xSPIMutex`
3. Empty `for(;;)` loops without blocking will starve other tasks
4. IR TX (`modulo_ir_send_nec`) blocks all interrupts for ~110ms — avoid during active captures

## Build
```bash
# From project root
make
```
Config in `config.mk` - FreeRTOS heap type 4, FatFS enabled
