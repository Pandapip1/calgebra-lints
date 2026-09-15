/* SPDX-FileCopyrightText: (C) 2026 Gavin John */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "../../include/ownership.h"

tokdef mode64_token;

grants_thread_token(mode64_token)
void enter_long_mode(void);

requires_thread_token(mode64_token)
void sixty_four_bit_only(void);

requires_thread_token_absent(mode64_token)
void thirty_two_bit_only(void);

void enter_long_mode(void) {}
void sixty_four_bit_only(void) {}
void thirty_two_bit_only(void) {}

/* Calls the 64-bit-only function without ever transitioning. */
void broken_caller(void) {
	sixty_four_bit_only(); /* thread-token-expect */
}

/* Transitions, then calls the portable-only function while the token
 * is still held. */
void broken_portable_caller(void) {
	enter_long_mode();
	thirty_two_bit_only(); /* thread-token-expect */
}
