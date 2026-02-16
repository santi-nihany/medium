# Context

Médium is an IR/RF signal capture/storage/replay device running FreeRTOS v10 on LPC4337 (EDU-CIAA). The RTOS skeleton is complete with working MicroSD storage and mocked signal generation. Three features developed in separate branches need integration: RF (CC1101 transceiver), IR (capture/transmit), and UI (SH1106 OLED display). Each will be integrated sequentially with conflict analysis.

---

# Phase 1: RF Module Integration

## Files to Add

| New File                | Adapted From                            | Purpose                                                       |
| ----------------------- | --------------------------------------- | ------------------------------------------------------------- |
| medium/inc/cc1101.h     | branch_rf/.../inc/c1101.h               | CC1101 register defs, config struct, SPI functions            |
| medium/src/cc1101.c     | branch_rf/.../src/c1101.c               | CC1101 driver (register read/write, init, freq/mod/bw config) |
| medium/inc/rf_capture.h | Adapted from branch_rf/.../inc/shield.h | Pin mappings, RFCaptureConfig_t struct, capture API           |
| medium/src/rf_capture.c | Adapted from branch_rf/.../src/shield.c | GPIO init, GDO0 capture polling, CC1101 setup wrappers        |

## Files to Modify

### medium/src/main.c

- Add `#include "cc1101.h"` and `#include "rf_capture.h"`
- Rename `xStorageMutex` → `xSPIMutex` (also update extern declarations in all files)
- In `initHardware()` after SD CS remap: call `cc1101_initGPIO()` (configures CS on GPIO0[15], GDO0 on GPIO3[3] as input, GDO2 on GPIO3[0] as input — must be after `spiConfig(SPI0)` to reclaim P6_1 from default SD CS)
- In `initHardware()`: call `cc1101_init()` with SPI mutex protection to detect and configure CC1101

### medium/inc/signal_capture.h

- Add `RFCaptureConfig_t` struct (freq_mhz, modulation_mode, bandwidth_khz, delay_us)
- Add `extern RFCaptureConfig_t xCurrentRFConfig` — set by UI before capture, read by Storage for file header

### medium/src/signal_capture.c — Replace idle `vSignalCaptureRF_Task`

- Task blocks on `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` waiting for UI command
- On notification: allocate `SignalPacket_t` with 4096-byte data area directly (avoids double-buffer)
- Take `xSPIMutex`, configure CC1101 for RX mode, release mutex (CC1101 is autonomous during capture — GDO0 is read via GPIO, not SPI)
- Raise priority to `configMAX_PRIORITIES - 1` for tight polling
- Capture GDO0 into `packet->data[]` using adapted `cmd_recraw()` logic with short `taskENTER_CRITICAL/EXIT_CRITICAL` bursts per byte (~80µs each)
- Restore priority, set `packet->mode = SIGNAL_MODE_RF`, `packet->sample_count = 4096` (byte count)
- Send packet to `xStorageQueue`
- Do NOT use `xStreamBufferRF` — RF capture is polling-based, not ISR-driven. StreamBufferRF can be removed to save ~2KB heap, or kept for future use

### medium/src/signal_storage.c

- Rename `xStorageMutex` → `xSPIMutex` (extern)
- When `packet->mode == SIGNAL_MODE_RF`: write RF-specific header format:

```
CC1101_CAPTURE;VER1\r\n
FREQ_MHZ=433.92\r\n
MODULATION=2\r\n
BANDWIDTH_KHZ=101.00\r\n
DELAY_US=10\r\n
DATA_LENGTH=4096\r\n
---DATA_START---\r\n
[4096 bytes binary]
```

- Read metadata from `xCurrentRFConfig` global
- When `packet->mode == SIGNAL_MODE_IR`: keep existing `MED1;VER1;...` header + uint32_t binary

### medium/config.mk

- No changes needed (SPI0 and FatFS already enabled)

## Critical: SPI0 Bus Sharing

- SD card CS: GPIO3[12] (P7_4) — CC1101 CS: GPIO0[15] (P1_20) — different CS pins, same SPI bus
- `xSPIMutex` serializes ALL SPI0 access (SD + CC1101)
- During RF capture polling loop, mutex is released (only GPIO reads, no SPI)
- After capture, Storage task takes mutex for SD write — no CC1101 SPI conflict since capture is done
- Init order is critical: `spiConfig(SPI0)` → SD CS remap (P7_4) → `cc1101_initGPIO()` (reclaims P6_1 for GDO2)

## GPIO Pin Map After RF

| Pin   | GPIO      | Use                                        |
| ----- | --------- | ------------------------------------------ |
| P7_4  | GPIO3[12] | SD card CS                                 |
| P1_20 | GPIO0[15] | CC1101 CS                                  |
| P6_4  | GPIO3[3]  | CC1101 GDO0 (signal I/O)                   |
| P6_1  | GPIO3[0]  | CC1101 GDO2 (reclaimed from default SD CS) |

## Heap Impact

- `SignalPacket_t` + 4096 data: ~4.1KB dynamic (allocated during capture, freed by Storage)
- CC1101 driver globals: ~40B static
- No static recording buffer — capture directly into `packet->data[]`
- Est. free heap: ~10KB remaining

## Conflict Analysis (RF only)

- SPI0: Protected by `xSPIMutex`. No conflict ✓
- GPIO3[0] (P6_1): Default SD CS conflicts with GDO2. Resolved by init ordering ✓
- Timers: RF uses `delayInaccurateUs()` (software busy-wait), no hardware timers. No conflict ✓
- Scheduler: Polling at µs intervals with raised priority blocks lower tasks for ~300ms. Acceptable for capture ✓
- Heap: Peak ~4.1KB during capture. Fits within budget ✓

---

# Phase 2: IR Module Integration

## Files to Add

| New File               | Adapted From                  | Purpose                                       |
| ---------------------- | ----------------------------- | --------------------------------------------- |
| medium/inc/ir_module.h | branch_ir/.../inc/modulo_ir.h | IRPulse_t, timing constants, capture/send API |
| medium/src/ir_module.c | branch_ir/.../src/modulo_ir.c | IR capture (ISR-driven), NEC decode, NEC TX   |

## Files to Modify

### medium/src/main.c

- Add `#include "ir_module.h"`
- In `initHardware()`: call `ir_init()` which:
  - Configures GPIO7 as input (IR RX) with pull-up
  - Configures GPIO5 as output (IR TX)
  - Initializes TIMER2 as free-running µs counter (prescaler = CLK/1MHz - 1)
  - Initializes TIMER0 for 38kHz carrier (half-period ~13µs match interrupt)
  - Initializes TIMER3 for NEC state machine timing

### medium/src/isr_handlers.c — Implement real IR_ISR_Handler

```c
void GPIO_IRQ_Handler(void)  // PIN_INT0_IRQn
{
    static uint32_t last_timestamp = 0;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    uint32_t now = Chip_TIMER_ReadCount(LPC_TIMER2);
    uint32_t delta = now - last_timestamp;
    last_timestamp = now;

    uint8_t level = Chip_GPIO_GetPinState(LPC_GPIO_PORT, IR_RX_GPIO_PORT, IR_RX_GPIO_PIN);
    uint32_t sample = (delta & 0x00FFFFFF) | ((uint32_t)level << 24);

    xStreamBufferSendFromISR(xStreamBufferIR, &sample, sizeof(uint32_t), &xHigherPriorityTaskWoken);
    Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(0));
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

### medium/src/main.c → `initInterrupts()`

- Configure GPIO7 (IR RX) as PININT channel 0, both edges
- Set NVIC priority to `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`
- Enable `PIN_INT0_IRQn`

### medium/src/signal_capture.c — `vSignalCaptureIR_Task`

- Already reads uint32_t samples from `xStreamBufferIR`
- Already packages into `SignalPacket_t` and sends to `xStorageQueue`
- Already handles burst detection with 100ms timeout
- Verify: task notification mechanism to enable/disable capture (from UI)

## IR TX (Timer-Driven, No `__disable_irq`)

- TIMER0 ISR: toggles GPIO5 at 38kHz
- TIMER3 ISR: NEC TX state machine
- Non-blocking: fires `xTaskNotifyGive()` to Replay task when TX complete
- `ir_tx_nec(uint8_t addr, uint8_t cmd)` starts TX, returns immediately

## Timer Allocation

| Timer    | Assignment                  | ISR Frequency | RTOS API?               |
| -------- | --------------------------- | ------------- | ----------------------- |
| TIMER0   | IR 38kHz carrier            | ~76kHz        | No                      |
| TIMER2   | µs reference (free-running) | None          | N/A                     |
| TIMER3   | NEC TX state machine        | Variable      | Yes (`xTaskNotifyGive`) |
| SysTick  | FreeRTOS                    | 100Hz         | Yes                     |
| Software | DiskTimer                   | 10ms          | Yes                     |

## Signal Format

- Native compatibility with medium format
- ISR packs `(delta_us & 0x00FFFFFF) | (level << 24)`
- Storage uses existing header + uint32_t samples
- No conversion needed

## Heap Impact

- StreamBufferIR: 2048B
- SignalPacket_t IR: ~1KB
- Timer state: ~20B
- Est. free heap: ~9KB

## Conflict Analysis (IR + RF)

- SPI0: No conflict ✓
- Timers: No overlap ✓
- GPIO: No overlap ✓
- PININT: Channel 0 only ✓
- Concurrent capture prevented by UI ✓
- TIMER0 ISR overhead acceptable ✓

---

# Phase 3: UI Module Integration

## Files to Add

| New File             | Adapted From                | Purpose                   |
| -------------------- | --------------------------- | ------------------------- |
| medium/inc/sh1106.h  | branch_ui/.../inc/sh1106.h  | SH1106 OLED driver (I2C0) |
| medium/src/sh1106.c  | branch_ui/.../src/sh1106.c  | OLED init/draw/update     |
| medium/inc/sprites.h | branch_ui/.../inc/sprites.h | Sprite declarations       |
| medium/src/sprites.c | branch_ui/.../src/sprites.c | Sprite bitmaps            |

## Files to Modify

### medium/src/main.c

- Add `#include "sh1106.h"`
- In `initHardware()`: `i2cInit(I2C0, 100000)` then `sh1106_init()`

### medium/src/ui_controller.c

- Replace printf stubs with OLED rendering
- UI task loop:
  - Queue receive (50ms)
  - Poll buttons (50ms debounce)
  - FSM transitions
  - Redraw on state change

- Trigger capture/replay via task notifications
- Display per state (MENU, CAPTURE, FINISHED, FILES, ERROR)

### Task Handles

- Make `xTaskSignalCaptureIR`, `xTaskSignalCaptureRF`, `xTaskReplay` accessible

## Button Input

- Polling via `gpioRead()`
- Simple debounce
- TEC mapping per available buttons

## Display Update Timing

- ~82ms per full update
- UI priority = 1
- Update only on dirty flag

## Heap Impact

- SH1106 framebuffer: 1024B static
- No dynamic allocation
- Est. free heap: ~9KB

## Conflict Analysis (UI + IR + RF)

- I2C0 isolated ✓
- SPI0 isolated ✓
- CPU preemption acceptable ✓
- UI freeze during capture acceptable ✓

---

# RTOS Configuration Changes

- Likely none required
- Heap and priorities sufficient

## Global Rename

- `xStorageMutex` → `xSPIMutex` across all relevant files

---

# Verification Plan

## After Phase 1 (RF)

1. CC1101 detection
2. RF capture → Storage queue
3. SD write verification
4. SPI sharing verification
5. Heap stability

## After Phase 2 (IR)

1. IR RX sampling
2. Storage verification
3. IR TX test
4. Timer impact check
5. Heap stability

## After Phase 3 (UI)

1. OLED splash
2. Menu navigation
3. IR capture flow
4. RF capture flow
5. File browsing
6. End-to-end replay

---

# Implementation Order Summary

1. Rename mutex
2. Add RF driver + capture
3. Wire RF
4. Update Storage
5. Test RF
6. Add IR module
7. Wire IR
8. Test IR
9. Add UI drivers
10. Rewrite UI controller
11. Test UI
