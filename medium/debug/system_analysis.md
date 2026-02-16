# Médium — System Flow & Analysis

## System Flow

### Boot Sequence
1. `main()` → `initHardware()`: configures UART, SPI0, remaps SD CS pin, initializes CC1101 (SPI), IR module (TIMER2 + GPIO), SH1106 OLED (I2C0)
2. `initRTOSPrimitives()`: creates StreamBuffers (IR/RF), StorageQueue, UICommandQueue, xSPIMutex, DiskTimer (10ms for FatFS)
3. `createTasks()`: 7 tasks — CaptureIR(4), CaptureRF(4), Storage(3), Replay(2), UI(1), Housekeeping(1), MockGen(1)
4. `initInterrupts()`: PIN_INT0 on IR RX pin, both-edge, NVIC priority safe for FreeRTOS
5. `vTaskStartScheduler()`

### Runtime Flow

**UI Task (priority 1)** — the user-facing control loop:
- Shows splash screen (Sprite_0001) for 2s, then enters 50ms poll loop
- Polls TEC1-TEC4 buttons with edge detection
- Processes UICommandQueue for async events from other tasks
- FSM: MENU → CAPTURE_IR/RF → FINISHED → MENU, or MENU → FILES → REPRODUCE
- On "Capturar IR" + OK: sends `xTaskNotifyGive(xTaskSignalCaptureIR)`
- On "Capturar RF" + OK: sends `xTaskNotifyGive(xTaskSignalCaptureRF)`
- On file select + OK: calls `Replay_Start()` + `xTaskNotifyGive(xTaskReplay)`
- Renders to SH1106 framebuffer only when `dirty` flag is set, then calls `sh1106_update()` (~82ms I2C transfer)

**IR Capture (priority 4)**:
- Blocks on `ulTaskNotifyTake()` until UI triggers
- ISR (`GPIO0_IRQHandler`) fires on each IR edge → reads TIMER2 µs timestamp → packs `(delta | level<<24)` → sends to `xStreamBufferIR`
- Task reads samples from StreamBuffer with 100ms burst timeout, accumulates up to 256 samples
- Allocates `SignalPacket_t` via `pvPortMalloc`, copies data, sends pointer to `xStorageQueue`

**RF Capture (priority 4)**:
- Blocks on `ulTaskNotifyTake()` until UI triggers
- Takes `xSPIMutex`, configures CC1101 (freq/mod/BW/RX), releases mutex
- Elevates self to `configMAX_PRIORITIES - 1` for tight GDO0 polling
- `rf_capture_raw()` polls GDO0 into 4096-byte buffer bit-by-bit
- Restores priority, wraps in `SignalPacket_t`, sends to `xStorageQueue`

**Storage Task (priority 3)**:
- Blocks on `xStorageQueue` (receives `SignalPacket_t*`)
- Mounts SD lazily on first write
- Takes `xSPIMutex`, generates sequential filename, writes header + binary data, releases mutex
- Frees packet via `vPortFree`
- IR format: `MED1;VER1;TS=...;MODE=...;SAMPLES=...\r\n` + uint32_t binary
- RF format: `CC1101_CAPTURE;VER1\r\n` + metadata lines + `---DATA_START---\r\n` + raw bytes

**Replay Task (priority 2)**: Currently a stub — prints hello every 4s, state machine is commented out.

---

## Issues Found

### Critical

1. **IR TX blocks all interrupts for ~68ms** (`ir_module.c:125-162`). `ir_send_nec_blocking()` calls `__disable_irq()` at line 125 and `__enable_irq()` at line 162. During a full NEC frame (~68ms), **all** FreeRTOS scheduling stops, the DiskTimer doesn't fire, and any IR signals arriving would be missed. This also violates FreeRTOS best practices. The plan mentioned timer-driven NEC TX (TIMER0 at 38kHz + TIMER3 for state machine) but the implementation uses blocking bitbang instead.

2. **Replay task is a stub** (`signal_replay.c:19-58`). The main loop just prints every 4s. The state machine is commented out. `Replay_Start()` sets the state to LOADING but nothing processes it. The UI's REPRODUCE screen calls `Replay_Start()` + `xTaskNotifyGive(xTaskReplay)` but the task never receives the notification (no `ulTaskNotifyTake`).

3. **`Storage_LoadSignal()` is not implemented** (`signal_storage.c:312-319`). Returns `pdFAIL` always. Replay depends on this to load files from SD — even once the Replay task is finished, it can't load data.

4. **Storage has a test limit of 5 packets** (`signal_storage.c:137,152`). `MAX_TEST_PACKETS = 5` causes all packets after the 5th to be silently discarded. This needs to be set to 0 (unlimited) for production.

### Moderate

5. **UI doesn't transition to FINISHED after capture completes.** The `HandleCapture()` function checks `SignalCapture_IsActive()` to redraw, but never transitions to `UI_STATE_FINISHED`. The capture tasks set `xIRCaptureActive = pdFALSE` when done but don't send a `UI_EVENT_CAPTURE_SUCCESS` to the queue. So after capture the user sees "Apunte el control y presione OK" again instead of the save/discard screen. Either:
   - The capture tasks should send `UI_EVENT_CAPTURE_SUCCESS` to `xUICommandQueue` when done, OR
   - The UI should detect the transition from active→inactive and auto-advance

6. **`SignalCapture_Start()`/`Stop()` are dead code.** The UI uses `xTaskNotifyGive()` directly to trigger captures (correct), but the old `SignalCapture_Start/Stop` functions just set flags without sending notifications. They're never called from the UI anymore.

7. **`Storage_ListFiles()` takes `xSPIMutex` and is called from UI task.** If the Storage task is holding the mutex for an SD write (which can take hundreds of ms), the UI will block for up to 5 seconds on the mutex. During that time, buttons won't respond and the display won't update. Not a bug, but bad UX. Consider either: calling it only when Storage is idle, or caching the file list.

8. **`file_list` filenames don't include the full path.** `Storage_ListFiles` stores only `fno.fname` (just the filename), but `Replay_Start` receives this short name. Later, `Storage_LoadSignal` (once implemented) will need the full path `SDC:/signals/filename.sig`. The path needs to be prepended somewhere.

9. **Mock signal generator is still enabled.** `vMockSignalGenerator_Task` is created in `main.c:298-306`. For production with real hardware, this should be disabled or it will inject fake signals.

### Minor

10. **`file_count` underflow in `HandleFiles`.** In `ui_controller.c` HandleFiles DOWN event: `if (cursor < file_count - 1)` — if `file_count` is 0, this is `(uint32_t)0 - 1 = 0xFFFFFFFF`, so the DOWN button would be allowed. The `file_count == 0` case is handled by the ACCEPT guard but not by UP/DOWN navigation. Add a `file_count > 0` guard.

11. **No UI feedback for SD mount failure.** If the SD card isn't inserted, `Storage_ListFiles` returns 0 (which shows "Sin archivos"), and captures get silently discarded by Storage. The user has no indication that the SD card is missing.

---

## Next Steps to Finish Project Scope

### Must-have (functional completion)

1. **Implement the Replay task** — Uncomment and complete `vReplay_Task`:
   - Block on `ulTaskNotifyTake()`
   - Call `Storage_LoadSignal()` to read file from SD
   - For IR: parse header + reconstruct pulse timings, call IR TX
   - For RF: parse header + configure CC1101 for TX + send raw data via GDO0
   - Notify UI when done

2. **Implement `Storage_LoadSignal()`** — Parse the header format (MED1 for IR, CC1101_CAPTURE for RF), allocate `SignalPacket_t`, read binary data, return to caller.

3. **Fix capture→finished UI transition** — After a capture task completes, it should send `UI_EVENT_CAPTURE_SUCCESS` to `xUICommandQueue` so the UI advances to the FINISHED screen. Add this at the end of both `vSignalCaptureIR_Task` (after `xQueueSend` to storage) and `vSignalCaptureRF_Task`.

4. **Replace blocking IR TX with timer-driven TX** — Use TIMER0 for 38kHz carrier toggle (ISR does only GPIO) and TIMER3 for NEC state machine timing. This avoids the 68ms `__disable_irq()` window that freezes the entire system.

5. **Remove test limits** — Set `MAX_TEST_PACKETS = 0` in `signal_storage.c` and disable/remove the mock signal generator task.

### Should-have (robustness)

6. **Fix `file_count` underflow** in `HandleFiles` DOWN event.

7. **Prepend directory path** to filenames when passing to `Replay_Start()` / `Storage_LoadSignal()`.

8. **Add SD card status indicator** on the UI menu (e.g., show "SD: OK" or "SD: ---" in a corner).

9. **Add file delete from UI** — The `Storage_DeleteSignal()` function exists but there's no UI path to trigger it (the `UI_STATE_DELETE_FILE` state from the enum is unused).

### Nice-to-have (polish)

10. **RF frequency selection screen** — Before RF capture, let the user choose from preset frequencies (315, 433.92, 868, 915 MHz) instead of using a hardcoded default.

11. **File type icons/labels** in the file browser — show "IR" or "RF" prefix based on `file_list[idx].mode`.

12. **LED feedback** — Toggle LED1/LED2 during capture/replay for visual hardware indication.
