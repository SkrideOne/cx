#ifndef BPF_ENDIAN_H
#define BPF_ENDIAN_H
// SPDX-License-Identifier: GPL-2.0
#include <byteswap.h>
#include <stdint.h>
static inline uint16_t __bpf_constant_htons(uint16_t v)
{
	return (v >> 8) | (v << 8);
}
static inline uint32_t __bpf_constant_htonl(uint32_t v)
{
	return __builtin_bswap32(v);
}
#define bpf_htons(x) __bpf_constant_htons(x)
#define bpf_htonl(x) __bpf_constant_htonl(x)
#endif
