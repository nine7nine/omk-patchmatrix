/*
 * SPDX-FileCopyrightText: Hanspeter Portner <dev@open-music-kontrollers.ch>
 * SPDX-License-Identifier: Artistic-2.0
 */

#ifndef _PATCHMATRIX_JACK_H
#define _PATCHMATRIX_JACK_H

#include <patchmatrix/patchmatrix.h>

int
_jack_init(app_t *app);

void
_jack_deinit(app_t *app);

bool
_jack_anim(app_t *app);

#endif
