/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2026 OpenStickCommunity (gp2040-ce.info)
 */

#define __BTSTACK_FILE__ "BLEHIDDriver.cpp"

#include "drivers/blehid/BLEHIDDriver.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/btstack_flash_bank.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include <cstring>

#include "gamepad/GamepadState.h"
#include "enums.pb.h"

extern "C" {
int blehid_get_dpad_mode(void);
}

extern "C" {
#include "btstack.h"
#include "ble/gatt-service/hids_device.h"
#include "blehid.h"
}

#undef HID_REPORT_TYPE_INPUT
#undef HID_REPORT_TYPE_OUTPUT
#undef HID_REPORT_TYPE_FEATURE

#define null_addr ((uint8_t*)0)

static hci_con_handle_t con_handle = HCI_CON_HANDLE_INVALID;
static uint8_t protocol_mode = 1; // Report mode
static bool input_report_enabled = false;
static bool can_send_requested = false;
static uint8_t last_report[7];
static uint8_t pending_report[7];
static bool report_pending = false;

static const uint8_t blehid_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    
    // 16 buttons
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x10,        //   Usage Maximum (Button 16)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x10,        //   Report Count (16)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    // Hat switch (D-pad)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x39,        //   Usage (Hat switch)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x07,        //   Logical Maximum (7)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x65, 0x14,        //   Unit (Eng Rot:Angular Pos)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x42,        //   Input (Data,Var,Abs,Null)
    
    // Padding (4 bits)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x03,        //   Input (Const,Var,Abs)
    
    // 4 axes (X, Y, Z, Rz) - 8 bits each
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x32,        //   Usage (Z)
    0x09, 0x35,        //   Usage (Rz)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    0xC0               // End Collection
};

static uint8_t dpad_to_hat(uint8_t dpad) {
    switch (dpad & 0x0F) {
        case 0x01: return 0;  // Up
        case 0x09: return 1;  // Up-Right
        case 0x08: return 2;  // Right
        case 0x0A: return 3;  // Down-Right
        case 0x02: return 4;  // Down
        case 0x06: return 5;  // Down-Left
        case 0x04: return 6;  // Left
        case 0x05: return 7;  // Up-Left
        default:   return 8;  // Neutral
    }
}

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            con_handle = HCI_CON_HANDLE_INVALID;
            input_report_enabled = false;
            gap_advertisements_enable(1);
            break;
        case HCI_EVENT_LE_META:
            if (hci_event_le_meta_get_subevent_code(packet) == HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
                con_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
            }
            break;
        case SM_EVENT_JUST_WORKS_REQUEST:
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            break;
        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            sm_numeric_comparison_confirm(sm_event_passkey_display_number_get_handle(packet));
            break;
        case HCI_EVENT_HIDS_META:
            switch (hci_event_hids_meta_get_subevent_code(packet)){
                case HIDS_SUBEVENT_INPUT_REPORT_ENABLE:
                    con_handle = hids_subevent_input_report_enable_get_con_handle(packet);
                    input_report_enabled = hids_subevent_input_report_enable_get_enable(packet) != 0;
                    break;
                case HIDS_SUBEVENT_PROTOCOL_MODE:
                    protocol_mode = hids_subevent_protocol_mode_get_protocol_mode(packet);
                    break;
                case HIDS_SUBEVENT_CAN_SEND_NOW:
                    can_send_requested = false;
                    if (con_handle != HCI_CON_HANDLE_INVALID && input_report_enabled) {
                        if (report_pending) {
                            if (protocol_mode == 1) {
                                hids_device_send_input_report(con_handle, pending_report, sizeof(pending_report));
                            } else {
                                hids_device_send_boot_keyboard_input_report(con_handle, pending_report, sizeof(pending_report));
                            }
                            memcpy(last_report, pending_report, sizeof(pending_report));
                            report_pending = false;
                        }
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

bool blehid_init(void) {
    if (cyw43_arch_init()) return false;

    l2cap_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_BONDING);

    att_server_init(profile_data, NULL, NULL);
    hids_device_init(0, blehid_report_descriptor, sizeof(blehid_report_descriptor));

    static btstack_packet_callback_registration_t hci_event_callback_registration;
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    static btstack_packet_callback_registration_t sm_event_callback_registration;
    sm_event_callback_registration.callback = &packet_handler;
    sm_add_event_handler(&sm_event_callback_registration);

    hids_device_register_packet_handler(packet_handler);

    // Setup advertising data
    uint8_t adv_data[] = {
        0x02, 0x01, 0x06,
        0x0A, 0x09, 'G', 'P', '2', '0', '4', '0', '-', 'C', 'E',
        0x03, 0x03, 0x12, 0x18,
        0x03, 0x19, 0xC4, 0x03
    };
    gap_advertisements_set_data(sizeof(adv_data), adv_data);
    gap_advertisements_set_params(0x0030, 0x0030, 0, 0, null_addr, 0x07, 0x00);
    gap_advertisements_enable(1);

    hci_power_control(HCI_POWER_ON);

    memset(last_report, 0, sizeof(last_report));
    memset(pending_report, 0, sizeof(pending_report));

    return true;
}

bool blehid_process(Gamepad *gamepad) {
    if (con_handle == HCI_CON_HANDLE_INVALID || !input_report_enabled || gamepad == nullptr) {
        return false;
    }

    uint8_t report[7];
    memset(report, 0, sizeof(report));

    uint16_t buttons = 0;
    if (gamepad->state.buttons & GAMEPAD_MASK_B1) buttons |= (1 << 0);
    if (gamepad->state.buttons & GAMEPAD_MASK_B2) buttons |= (1 << 1);
    if (gamepad->state.buttons & GAMEPAD_MASK_B3) buttons |= (1 << 2);
    if (gamepad->state.buttons & GAMEPAD_MASK_B4) buttons |= (1 << 3);
    if (gamepad->state.buttons & GAMEPAD_MASK_L1) buttons |= (1 << 4);
    if (gamepad->state.buttons & GAMEPAD_MASK_R1) buttons |= (1 << 5);
    if (gamepad->state.buttons & GAMEPAD_MASK_L2) buttons |= (1 << 6);
    if (gamepad->state.buttons & GAMEPAD_MASK_R2) buttons |= (1 << 7);
    if (gamepad->state.buttons & GAMEPAD_MASK_S1) buttons |= (1 << 8);
    if (gamepad->state.buttons & GAMEPAD_MASK_S2) buttons |= (1 << 9);
    if (gamepad->state.buttons & GAMEPAD_MASK_L3) buttons |= (1 << 10);
    if (gamepad->state.buttons & GAMEPAD_MASK_R3) buttons |= (1 << 11);
    if (gamepad->state.buttons & GAMEPAD_MASK_A1) buttons |= (1 << 12);
    if (gamepad->state.buttons & GAMEPAD_MASK_A2) buttons |= (1 << 13);

    report[0] = buttons & 0xFF;
    report[1] = (buttons >> 8) & 0xFF;

    DpadMode dpadMode = (DpadMode)blehid_get_dpad_mode();
    uint8_t hat = 8;
    if (dpadMode == DPAD_MODE_DIGITAL) {
        hat = dpad_to_hat(gamepad->state.dpad);
    }
    report[2] = hat & 0x0F;

    uint8_t lx = gamepad->state.lx >> 8;
    uint8_t ly = gamepad->state.ly >> 8;
    uint8_t rx = gamepad->state.rx >> 8;
    uint8_t ry = gamepad->state.ry >> 8;

    if (dpadMode == DPAD_MODE_LEFT_ANALOG) {
        if (gamepad->state.dpad & (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT)) {
            lx = 128;
            ly = 128;
            if (gamepad->state.dpad & GAMEPAD_MASK_LEFT)  lx = 0;
            if (gamepad->state.dpad & GAMEPAD_MASK_RIGHT) lx = 255;
            if (gamepad->state.dpad & GAMEPAD_MASK_UP)    ly = 0;
            if (gamepad->state.dpad & GAMEPAD_MASK_DOWN)  ly = 255;
        }
    }

    if (dpadMode == DPAD_MODE_RIGHT_ANALOG) {
        if (gamepad->state.dpad & (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT)) {
            rx = 128;
            ry = 128;
            if (gamepad->state.dpad & GAMEPAD_MASK_LEFT)  rx = 0;
            if (gamepad->state.dpad & GAMEPAD_MASK_RIGHT) rx = 255;
            if (gamepad->state.dpad & GAMEPAD_MASK_UP)    ry = 0;
            if (gamepad->state.dpad & GAMEPAD_MASK_DOWN)  ry = 255;
        }
    }

    report[3] = lx;
    report[4] = ly;
    report[5] = rx;
    report[6] = ry;

    if (memcmp(report, last_report, sizeof(report)) != 0) {
        memcpy(pending_report, report, sizeof(report));
        report_pending = true;
        if (!can_send_requested) {
            if (hids_device_request_can_send_now_event(con_handle) == ERROR_CODE_SUCCESS) {
                can_send_requested = true;
            }
        }
        return true;
    }

    return false;
}

bool blehid_is_connected(void) {
    return con_handle != HCI_CON_HANDLE_INVALID;
}

void blehid_clear_bonds(void) {
    multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(PICO_FLASH_BANK_STORAGE_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();
}
