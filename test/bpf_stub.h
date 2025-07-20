#ifndef BPF_STUB_H
#define BPF_STUB_H
// Purpose: Lightweight BPF helper stubs for unit tests
// Pipeline: clang-format > clang-tidy > custom lint > build > test
// Actions: emulate kernel helpers in user space
// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stdint.h>
#include <string.h>

#define SEC(name)
#define __uint(name, val)
#define __type(name, val)
#define __array(name, val)
#define __aligned(x)
#define __ksym
#define BPF_OK 0
#define BPF_ERR (-EFAULT)
#define BPF_KTIME_BASE 0
#ifndef __always_inline
#define __always_inline inline
#endif

typedef uint8_t	 __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint16_t __be16;
typedef uint32_t __be32;
typedef uint64_t __u64;
typedef int32_t	 __s32;
typedef int64_t	 __s64;

struct xdp_md {
	void* data;
	void* data_end;
};

static inline int bpf_xdp_load_bytes(struct xdp_md* ctx, int off, void* to,
				     __u32 len)
{
	if ((char*)ctx->data + off + len > (char*)ctx->data_end)
		return BPF_ERR;
	memcpy(to, (char*)ctx->data + off, len);
	return BPF_OK;
}

extern void*	     mock_map_value;
static unsigned char mock_storage[64];

static inline void* bpf_map_lookup_elem(void* map, const void* key) // NOLINT
{
	(void)map;
	(void)key;
	return mock_map_value;
}

static inline long bpf_map_update_elem(void* map, const void* key,
				       const void* val, __u64 flags) // NOLINT
{
	(void)map;
	(void)key;
	(void)flags;
	memcpy(mock_storage, val, sizeof(mock_storage));
	mock_map_value = mock_storage;
	return BPF_OK;
}
static inline long bpf_map_delete_elem(void* map, const void* key) // NOLINT
{
	(void)map;
	(void)key;
	return BPF_OK;
}
static inline void* bpf_map_lookup_percpu_elem(void* map, const void* key,
					       __u32 cpu) // NOLINT
{
	(void)map;
	(void)key;
	(void)cpu;
	return NULL;
}

static inline __u64 bpf_ktime_get_ns(void)
{
	return BPF_KTIME_BASE;
}
#ifndef bpf_htons
static inline __u16 bpf_htons(__u16 x)
{
	return (x << 8) | (x >> 8);
}
#endif
#ifndef bpf_htonl
static inline __u32 bpf_htonl(__u32 x)
{
	return __builtin_bswap32(x);
}
#endif
#ifndef bpf_ntohs
static inline __u16 bpf_ntohs(__u16 x)
{
	return bpf_htons(x);
}
#endif
#ifndef bpf_ntohl
static inline __u32 bpf_ntohl(__u32 x)
{
	return bpf_htonl(x);
}
#endif

#define bpf_probe_read_kernel(dest, size, src)                     \
    ({                                                          \
        if (src)                                                \
            memcpy(dest, src, size);                            \
        else                                                    \
            memset(dest, 0, size);                              \
        0;                                                      \
    })
#define bpf_tail_call(ctx, map, idx) do { } while(0)
#define bpf_tail_call_static(ctx, map, idx) do { } while(0)
#define BPF_ANY 0
#define BPF_NOEXIST 1
#define BPF_F_NO_COMMON_LRU 0
#define BPF_F_RDONLY_PROG 0
#define BPF_F_NO_PREALLOC 0
#define BPF_F_ZERO_SEED 0
#define LIBBPF_PIN_BY_NAME 0
#define XDP_PASS 2
#define XDP_DROP 1

#endif
