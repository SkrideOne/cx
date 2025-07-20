#ifndef VMLINUX_H
#define VMLINUX_H
// Purpose: Minimal kernel types for tests
// Pipeline: clang-format > clang-tidy > custom lint > build > test
// Actions: provide struct in6_addr
// SPDX-License-Identifier: GPL-2.0
#include <stdint.h>
struct in6_addr {
	uint8_t s6_addr[16];
};
#endif
