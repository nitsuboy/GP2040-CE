/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2026 OpenStickCommunity (gp2040-ce.info)
 */

#ifndef _BLEHIDDRIVER_H_
#define _BLEHIDDRIVER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

class Gamepad;

bool blehid_init(void);
bool blehid_process(Gamepad *gamepad);
bool blehid_is_connected(void);
void blehid_clear_bonds(void);

#ifdef __cplusplus
}
#endif

#endif
