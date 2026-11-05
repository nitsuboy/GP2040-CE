# Skill: BLE HID Gamepad Development for GP2040-CE

## Description
Use this skill when working on the Bluetooth LE HID Gamepad feature (`docs/ble-hid-plan.md`) in GP2040-CE. It enforces strict task-by-task execution, explicit user approval gates, scope boundaries, and verification procedures.

## Core Rules & Workflow

1. **Plan First, Implement Second:** Never make code changes without an approved plan and explicit user command.
2. **Task-by-Task Execution:**
   - Execute only one step/task at a time.
   - Check off tasks as completed in real time.
   - **Wait for user command** before starting the next task.
3. **Strict Scope Control:** Stay within the defined scope. Do not wander into out-of-scope items (e.g., Switch Classic Bluetooth port, Xbox controller BLE emulation, simultaneous USB+BLE output).

---

## Scope List

### In Scope
- Input Mode: `INPUT_MODE_BLE_HID` (18) and frontend/protobuf integration.
- Hardware / Boards: PicoW and Pico2W with BTstack BLE HOGP.
- BLE Driver: Generic BLE HID Gamepad driver (7-byte reports: buttons, hat, LX, LY, RX, RY).
- Runtime: Mutually exclusive selection via Input Mode (USB OR Bluetooth, never simultaneous, requiring save + reboot).
- Bond Management: TLV flash bank storage (`0x1F6000`) and clear-bonds functionality.
- Display & Web Config: BLE status indicators on display and web settings (including pairing forget button).

### Out of Scope (Prohibited)
- Porting Nintendo Switch Classic Bluetooth driver as primary implementation.
- Emulating proprietary Xbox controller Bluetooth identity.
- Simultaneous USB and BLE output.
- Hot-swapping transport without reboot.
- Custom BLE pairing flash sectors (use standard BTstack TLV bank).
- Changing Boot Mode Mapping logic.
- Adding input mode to profiles.

---

## Implementation Steps & Tasks

### Step 0 — Approval Gate
- [ ] User explicitly approves implementation.
- [ ] Confirm defaults in `docs/ble-hid-plan.md` are accepted.

### Step 1 — Register the BLE Input Mode
- [ ] Add `INPUT_MODE_BLE_HID = 18` in `proto/enums.proto`.
- [ ] Regenerate protobuf C/TS outputs.
- [ ] Add BLE to shared input-mode list and boot input-mode list.
- [ ] Add display name macro and map BLE display layout to XInput label row.
- [ ] Add English locale label.

### Step 2 — Build and Board Foundation
- [ ] Add BLE-only BTstack configuration (`headers/btstack_config.h`).
- [ ] Add CMake block for PicoW and Pico2W (`CMakeLists.txt`, `configs/Pico2W/...`).
- [ ] Link `pico_btstack_cyw43`, `pico_btstack_ble`, `pico_cyw43_arch_none`.
- [ ] Set `GP2040_BLUETOOTH_ENABLED=1` and `PICO_FLASH_BANK_STORAGE_OFFSET=0x1F6000`.
- [ ] Generate GATT header with `pico_btstack_make_gatt_header()`.

### Step 3 — Implement the BLE HID Core
- [ ] Define public API (`blehid_init`, `blehid_process`, `blehid_is_connected`, `blehid_clear_bonds`).
- [ ] Implement CYW43/BTstack initialization, L2CAP, security manager, ATT server, and HIDS setup.
- [ ] Configure Just Works secure pairing with bonding.
- [ ] Configure advertising (device name `GP2040-CE`, HID UUID `0x1812`, gamepad appearance `0x03C4`).
- [ ] Implement 7-byte report builder and button/axis mapping.
- [ ] Send reports via `HIDS_SUBEVENT_CAN_SEND_NOW`.
- [ ] Implement BTstack flash-bank bond clearing.

### Step 4 — Integrate USB vs BLE Runtime Selection
- [ ] Update `DriverManager::setup()` to handle `INPUT_MODE_BLE_HID` without creating a USB driver.
- [ ] Branch `GP2040::setup()` and `GP2040::run()` between USB and BLE.
- [ ] Ensure BLE mode preserves gamepad reading, debounce, addons, hotkeys, profiles, save, and reboot.
- [ ] Add fallback to XInput if BLE initialization fails.

### Step 5 — Add Display Support
- [ ] Add BLE display name and status-bar case.
- [ ] Show `BLE` or connected status.

### Step 6 — Add Web Settings and Bond Management
- [ ] Add `bluetoothEnabled` to firmware version and system stats.
- [ ] Hide/show BLE options based on firmware capability.
- [ ] Add "Forget Bluetooth Pairings" endpoint and UI.

### Step 7 — End-to-End Validation
- [ ] Build and test Pico (non-BT), PicoW (BT), and Pico2W (BT).
- [ ] Validate pairing, input reporting, reconnection, and bond clearing on Windows/Android.
- [ ] Verify firmware image size stays below `0x1F6000`.

### Step 8 — Cleanup and Acceptance
- [ ] Remove temporary scaffolding and review diffs.
