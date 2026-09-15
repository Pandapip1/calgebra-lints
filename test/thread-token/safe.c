/* SPDX-FileCopyrightText: (C) 2026 Gavin John */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* The generic, family-agnostic half of ErrnoDisciplineChecker's
 * thread-token pass: grants_thread_token/requires_thread_token/
 * drops_thread_token/requires_thread_token_absent for a caller-named
 * family that has nothing to do with errno, exercising the same
 * checker's forEachThreadTokenFamily-driven code path with a
 * non-errno_grounds family. */
#include "../../include/ownership.h"

tokdef mode64_token;

grants_thread_token(mode64_token)
void enter_long_mode(void);

requires_thread_token(mode64_token)
void sixty_four_bit_only(void);

drops_thread_token(mode64_token)
void enter_protected_mode(void);

requires_thread_token_absent(mode64_token)
void thirty_two_bit_only(void);

void enter_long_mode(void) {}
void sixty_four_bit_only(void) {}
void enter_protected_mode(void) {}
void thirty_two_bit_only(void) {}

/* Transitions first, then calls the gated function: satisfied. */
void correct_caller(void) {
	enter_long_mode();
	sixty_four_bit_only();
}

/* Never transitions, so the portable-only function's absence
 * requirement is satisfied. */
void correct_portable_caller(void) {
	thirty_two_bit_only();
}

/* Transitions, then drops back before calling the portable-only
 * function: absence requirement is satisfied again after the drop. */
void correct_round_trip(void) {
	enter_long_mode();
	enter_protected_mode();
	thirty_two_bit_only();
}
