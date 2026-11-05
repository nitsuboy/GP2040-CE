/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2026 OpenStickCommunity (gp2040-ce.info)
 */

#include "storagemanager.h"
#include "enums.pb.h"

extern "C" {
DpadMode blehid_get_dpad_mode(void) {
    return Storage::getInstance().getGamepadOptions().dpadMode;
}
}
