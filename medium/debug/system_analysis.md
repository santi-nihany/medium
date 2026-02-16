# Médium — System Flow & Analysis

## System Flow

### Boot Sequence
1. `main()` → `initHardware()`: configures UART, SPI0, remaps SD CS pin, initializes CC1101 (SPI GPIO + detect + rf_setup), IR module via `modulo_ir_init()` (TIMER2 + GPIO5/GPIO7), SH1106 OLED (I2C0 at 100kHz)
2. `initRTOSPrimitives()`: creates StreamBuffers (IR 2048B / RF 2048B), StorageQueue (10), UICommandQueue (20), xSPIMutex, DiskTimer (10ms for FatFS)
3. `createTasks()`: 7 tasks — CaptureIR(4), CaptureRF(4), Storage(3), Replay(2), UI(1), Housekeeping(1), MockGen(1)
4. `initInterrupts()`: PIN_INT0 on IR RX pin (GPIO port 5, pin 15), both-edge, NVIC priority = `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`
5. `vTaskStartScheduler()`

### Runtime Flow

**UI Task (priority 1)** — the user-facing control loop:
- Shows splash screen (Sprite_0001) for 2s, then enters 50ms poll loop
- Polls TEC1-TEC4 buttons with edge detection (falling edge = press, active low)
- Processes UICommandQueue for async events (UI_EVENT_CAPTURE_SUCCESS, UI_EVENT_CAPTURE_ERROR)
- FSM: MENU → CAPTURE_IR/RF → FINISHED → MENU, or MENU → FILES → REPRODUCE
- On "Capturar IR" + OK: sends `xTaskNotifyGive(xTaskSignalCaptureIR)`
- On "Capturar RF" + OK: sends `xTaskNotifyGive(xTaskSignalCaptureRF)`
- On file select + OK: calls `Replay_Start()` + `xTaskNotifyGive(xTaskReplay)`
- Renders to SH1106 framebuffer only when `dirty` flag is set, then calls `sh1106_update()` (~82ms I2C transfer)

**IR Capture (priority 4)**:
- Blocks on `ulTaskNotifyTake()` until UI triggers
- ISR (`GPIO0_IRQHandler` in `isr_handlers.c`) fires on each IR edge → reads TIMER2 µs timestamp → packs `(delta & 0x00FFFFFF | level<<24)` → sends to `xStreamBufferIR` via `xStreamBufferSendFromISR`
- Task reads samples from StreamBuffer with 100ms burst timeout, accumulates up to 256 uint32_t samples
- Allocates `SignalPacket_t` via `pvPortMalloc`, copies data, sends pointer to `xStorageQueue`

**RF Capture (priority 4)**:
- Blocks on `ulTaskNotifyTake()` until UI triggers
- Takes `xSPIMutex`, configures CC1101 (freq/mod/BW/pktFormat=3/RX), releases mutex
- Elevates self to `configMAX_PRIORITIES - 1` for tight GDO0 polling
- `rf_capture_raw()` polls GDO0 into 4096-byte buffer bit-by-bit (8 samples per byte)
- Restores priority, wraps in `SignalPacket_t` (sample_count = 4096 = byte count), sends to `xStorageQueue`

**Storage Task (priority 3)**:
- Blocks on `xStorageQueue` (receives `SignalPacket_t*`)
- Mounts SD lazily on first write (14MHz SPI, 500ms stabilization)
- Takes `xSPIMutex`, generates sequential filename (`signal_IR_XXXXXX.sig` / `signal_RF_XXXXXX.sig`), writes header + binary data, releases mutex
- Frees packet via `vPortFree`
- IR format: `MED1;VER1;TS=<ms>;MODE=0;SAMPLES=<n>\r\n` + uint32_t binary samples
- RF format: `CC1101_CAPTURE;VER1\r\n` + metadata lines (FREQ/MOD/BW/DELAY/LENGTH) + `---DATA_START---\r\n` + raw bytes

**Replay Task (priority 2)**: Currently a stub — prints "hello from signal replay" every 4s. State machine is commented out. Does not block on `ulTaskNotifyTake()`.

**Housekeeping (priority 1)**: Prints free heap and min heap every 3s via `vTaskDelayUntil`.

**MockGen (priority 1)**: Generates 64-sample bursts to `xStreamBufferIR` every 5s (square wave, delta=500, alternating level).

---

## Modules Integrated

### RF Module (CC1101 via SPI0)
- **Files**: `cc1101.c/h`, `rf_capture.c/h`
- **Pins**: CS=GPIO0[15] (P1_20), GDO0=GPIO3[3] (P6_4), GDO2=GPIO3[0] (P6_1, reclaimed from default SD CS)
- **SPI sharing**: xSPIMutex serializes all SPI0 access (SD + CC1101). During GDO0 polling capture, mutex is released (GPIO-only, no SPI)
- **Config**: `xCurrentRFConfig` global (freq_mhz, modulation_mode, bandwidth_khz, delay_us) — set by UI before capture, read by Storage for file header

### IR Module (modulo_ir)
- **Files**: `modulo_ir.c/h` (adapted from branch_ir)
- **Pins**: RX=GPIO7 (input), TX=GPIO5 (output)
- **Timers**: TIMER2 = free-running µs counter (polled, no ISR)
- **Capture (RTOS)**: ISR-driven via PININT ch0 → xStreamBufferIR → vSignalCaptureIR_Task. This is the primary capture path.
- **Capture (polling)**: `modulo_ir_capture()` — blocking poll at 30µs, returns IRPulse_t array. Available but not used by RTOS tasks.
- **NEC decode**: `modulo_ir_decode()` — validates header/bits/checksum, outputs addr+cmd
- **NEC TX**: `modulo_ir_send_nec()` — blocking, `__disable_irq()` for ~110ms
- **Carrier**: `irCarrierOn()`/`irCarrierOff()` via sAPI Timer_0 callback at ~13µs (38kHz). Non-blocking carrier infrastructure exists but is NOT used by `modulo_ir_send_nec()`.

### UI Module (SH1106 OLED)
- **Files**: `sh1106.c/h`, `sprites.c/h`, `ui_controller.c/h`
- **Bus**: I2C0 at 100kHz, address 0x3C
- **Framebuffer**: 1024B static (BSS)
- **States**: MENU, CAPTURE_IR, CAPTURE_RF, FINISHED, FILES, REPRODUCE, DELETE_FILE, ERROR
- **Buttons**: TEC1=UP, TEC2=DOWN, TEC3=ACCEPT, TEC4=BACK (polling at 50ms)

---

## Issues Found

### Critical

1. **IR TX blocks all interrupts for ~110ms** (`modulo_ir.c:105-140`). `modulo_ir_send_nec()` calls `__disable_irq()` at line 105 and `__enable_irq()` at line 140. During a full NEC frame, **all** FreeRTOS scheduling stops, the DiskTimer doesn't fire, and RTOS tick counter loses ~11 ticks. The non-blocking carrier infrastructure (`irCarrierOn`/`irCarrierOff` via Timer_0 callback) exists in the same file but is **not** integrated into the NEC TX function — it would need a timer-driven NEC state machine (TIMER3) to replace the blocking approach.

2. **Replay task is a stub** (`signal_replay.c:19-58`). The main loop just prints every 4s. The state machine is commented out. `Replay_Start()` sets the state to LOADING but nothing processes it. The task does not call `ulTaskNotifyTake()` so UI notifications are never received.

3. **`Storage_LoadSignal()` is not implemented** (`signal_storage.c:312-319`). Returns `pdFAIL` always. Replay depends on this to load files from SD. Must parse both MED1 (IR) and CC1101_CAPTURE (RF) header formats.

4. **Storage has a test limit of 5 packets** (`signal_storage.c:137`). `MAX_TEST_PACKETS = 5` causes all packets after the 5th to be silently discarded. Needs to be set to 0 (unlimited) for production.

### Moderate

5. **Capture tasks don't notify UI when capture completes.** The UI's `ProcessQueueCommands()` (`ui_controller.c:452-475`) correctly handles `UI_EVENT_CAPTURE_SUCCESS` → transitions to `UI_STATE_FINISHED`. However, neither `vSignalCaptureIR_Task` nor `vSignalCaptureRF_Task` in `signal_capture.c` send this event to `xUICommandQueue` after enqueuing the packet. Result: after capture finishes, the UI stays on the capture screen showing "Apunte el control y presione OK" instead of the save/discard screen. **Fix**: add `UICommand_t cmd = {UI_EVENT_CAPTURE_SUCCESS, 0}; xQueueSend(xUICommandQueue, &cmd, 0);` after successful `xQueueSend(xStorageQueue, ...)` in both capture tasks.

6. **`SignalCapture_Start()`/`Stop()` are dead code** (`signal_capture.c:192-224`). The UI uses `xTaskNotifyGive()` directly to trigger captures. These functions only set flags without sending task notifications and are never called. Can be removed or rewired.

7. **`Storage_ListFiles()` blocks UI on xSPIMutex** (`signal_storage.c:331`). If Storage task holds the mutex during an SD write (can take hundreds of ms), the UI blocks for up to 5 seconds. Buttons won't respond and display won't update. Consider caching the file list or calling it only when Storage is idle.

8. **File list filenames don't include full path** (`signal_storage.c:350`). `Storage_ListFiles` stores only `fno.fname` (e.g., `signal_IR_000001.sig`). `Replay_Start()` receives this short name. `Storage_LoadSignal` (once implemented) will need the full path `SDC:/signals/signal_IR_000001.sig`. The `SIGNAL_DIR` prefix must be prepended somewhere.

9. **Mock signal generator still enabled** (`main.c:298-306`). `vMockSignalGenerator_Task` injects fake 64-sample bursts into `xStreamBufferIR` every 5s, which triggers real IR capture + SD storage. Must be disabled when using real hardware, or it contaminates real captures.

10. **`modulo_ir_send_nec()` shares `carrierPhase` with `carrierCallback()`** (`modulo_ir.c:16,107,59`). The blocking NEC TX function manually toggles the static `carrierPhase` variable. If `irCarrierOn()` was previously active (e.g., from a failed prior operation), `carrierPhase` state may be inconsistent when Timer_0 resumes after `__enable_irq()`. In practice this is unlikely since both shouldn't run simultaneously, but there's no guard.

### Minor

11. **`file_count` underflow in `HandleFiles`** (`ui_controller.c:386`). DOWN event: `if (cursor < file_count - 1)` — if `file_count` is 0, this evaluates as `cursor < 0xFFFFFFFF` (unsigned underflow), allowing navigation into garbage. Add a `file_count > 0` guard.

12. **No UI feedback for SD mount failure.** If the SD card isn't inserted, `Storage_ListFiles` returns 0 (shows "Sin archivos"), and captured packets are silently discarded by Storage. The user has no indication the SD card is missing.

13. **`xStreamBufferRF` is allocated but unused** (`main.c:172`). RF capture uses polling (not ISR-driven), so it captures directly into the packet buffer. The 2048-byte StreamBufferRF is never written to or read from. Could be removed to save ~2KB heap.

---

## GPIO Pin Map

| Pin | GPIO | Use | Module |
|-----|------|-----|--------|
| P7_4 | GPIO3[12] | SD card CS | Storage |
| P1_20 | GPIO0[15] | CC1101 CS | RF |
| P6_4 | GPIO3[3] | CC1101 GDO0 (signal I/O) | RF |
| P6_1 | GPIO3[0] | CC1101 GDO2 (reclaimed from default SD CS) | RF |
| (sAPI GPIO7) | Port 5, Pin 15 | IR RX input + PININT ch0 | IR |
| (sAPI GPIO5) | TBD | IR TX output | IR |
| I2C0 SDA/SCL | — | SH1106 OLED | UI |
| TEC1-TEC4 | — | Buttons (UP/DN/OK/BACK) | UI |

## Timer Allocation

| Timer | Use | ISR? |
|-------|-----|------|
| SysTick | FreeRTOS tick (100Hz) | Yes — kernel |
| TIMER0 | IR 38kHz carrier (via sAPI Timer_Init callback) | Yes — ~76kHz toggle, only when `irCarrierOn()` active |
| TIMER2 | IR µs counter (free-running, polled) | No |
| Software (FreeRTOS) | DiskTimer 10ms for FatFS | Via timer daemon task |

## Heap Budget (24KB total)

| Allocation | Size | Type |
|------------|------|------|
| RTOS kernel + task stacks (7 tasks) | ~10KB | Static (at task creation) |
| StreamBufferIR | 2048B | Static (at creation) |
| StreamBufferRF | 2048B | Static (at creation, **unused**) |
| Queues + mutex + timer | ~500B | Static (at creation) |
| SH1106 framebuffer | 1024B | Static BSS (not heap) |
| IR pulseBuffer[200] | 1000B | Static BSS (not heap) |
| IR SignalPacket_t (during capture) | ~1KB | Dynamic (freed by Storage) |
| RF SignalPacket_t (during capture) | ~4.1KB | Dynamic (freed by Storage) |
| **Estimated free heap** | **~9KB** | |

---

## Next Steps to Finish Project Scope

### Must-have (functional completion)

1. **Implement the Replay task** — Replace stub with real implementation:
   - Block on `ulTaskNotifyTake()`
   - Call `Storage_LoadSignal()` to read file from SD
   - For IR: reconstruct pulse timings from uint32_t samples, call `modulo_ir_send_nec(addr, cmd)` if NEC, or raw replay via GPIO5
   - For RF: parse header, configure CC1101 for TX, send raw data via GDO0
   - Send `UI_EVENT_CAPTURE_SUCCESS` or similar to UI when done

2. **Implement `Storage_LoadSignal()`** — Parse header (detect MED1 vs CC1101_CAPTURE format), allocate `SignalPacket_t`, read binary data, return to caller. Must take xSPIMutex.

3. **Fix capture→finished UI transition** — Add `UI_EVENT_CAPTURE_SUCCESS` send to `xUICommandQueue` at end of both `vSignalCaptureIR_Task` and `vSignalCaptureRF_Task` after successful `xQueueSend` to storage.

4. **Remove test limits** — Set `MAX_TEST_PACKETS = 0` in `signal_storage.c`. Disable or guard the mock signal generator task (comment out `xTaskCreate` in `main.c` or add a `#ifdef MOCK_ENABLED` guard).

### Should-have (robustness)

5. **Replace blocking IR TX with timer-driven TX** — Use existing `irCarrierOn()`/`irCarrierOff()` (Timer_0) for carrier, add TIMER3 for NEC state machine timing. This avoids the ~110ms `__disable_irq()` window that freezes the entire RTOS.

6. **Fix `file_count` underflow** in `HandleFiles` DOWN event (`ui_controller.c:386`). Add `file_count > 0 &&` before `cursor < file_count - 1`.

7. **Prepend `SIGNAL_DIR` path** to filenames when passing to `Replay_Start()` / `Storage_LoadSignal()`.

8. **Remove unused `xStreamBufferRF`** from `initRTOSPrimitives()` to save ~2KB heap.

9. **Remove dead code** `SignalCapture_Start()`/`Stop()` from `signal_capture.c`.

### Nice-to-have (polish)

10. **Add SD card status indicator** on the UI menu screen (e.g., "SD: OK" / "SD: ---").

11. **RF frequency selection screen** — Before RF capture, let user choose from preset frequencies (315, 433.92, 868, 915 MHz).

12. **File type icons/labels** in file browser — show "IR" or "RF" prefix based on filename.

13. **LED feedback** — Toggle LED1/LED2 during capture/replay.

14. **Implement file delete from UI** — `Storage_DeleteSignal()` exists but `UI_STATE_DELETE_FILE` is unused.
