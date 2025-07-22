// Purpose: Unit tests for XDP program
// Pipeline: clang-format > clang-tidy > custom lint > build > test
// Actions: validate IPv4/IPv6 parsing and ACL logic
// SPDX-License-Identifier: GPL-2.0

// Test environment - we don't include vmlinux.h in tests
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
// clang-format off
#include <cmocka.h>
// clang-format on
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Basic type definitions for tests
typedef uint8_t	 __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef int32_t	 __s32;
typedef int64_t	 __s64;

#ifdef __always_inline
#undef __always_inline
#endif
#define __always_inline inline
#define SEC(x)
#define __aligned(x)

// BPF map macros for tests
#define __uint(name, val)
#define __type(name, val)
#define __array(name, val)

// Test-specific definitions
#define XDP_PASS 2
#define XDP_DROP 1
#define ETH_HLEN 14
#define ETH_P_IP 0x0800
#define ETH_P_IPV6 0x86DD
#define PROTO_TCP 6
#define PROTO_UDP 17
#define PROTO_ICMP 1
#define PROTO_ICMP6 58
#define AF_INET 2
#define AF_INET6 10

// BPF map type definitions for tests
#define BPF_MAP_TYPE_HASH 1
#define BPF_MAP_TYPE_ARRAY 2
#define BPF_MAP_TYPE_PROG_ARRAY 3
#define BPF_MAP_TYPE_PERCPU_HASH 5
#define BPF_MAP_TYPE_PERCPU_ARRAY 6
#define BPF_MAP_TYPE_LRU_HASH 9
#define BPF_MAP_TYPE_LRU_PERCPU_HASH 10

// BPF flags
#define BPF_ANY 0
#define BPF_NOEXIST 1
#define BPF_F_NO_PREALLOC (1U << 0)
#define BPF_F_NO_COMMON_LRU (1U << 1)
#define BPF_F_RDONLY_PROG (1U << 7)
#define BPF_F_ZERO_SEED (1U << 6)
#define LIBBPF_PIN_BY_NAME 1
#define BPF_OK 0
#define BPF_ERR (-1)

// Test-specific xdp_md structure
struct xdp_md {
	void* data;
	void* data_end;
};

// Test-specific in6_addr structure
struct in6_addr {
	union {
		__u8  u6_addr8[16];
		__u16 u6_addr16[8];
		__u32 u6_addr32[4];
	} in6_u;
#define s6_addr in6_u.u6_addr8
#define s6_addr32 in6_u.u6_addr32
};

#define TEST_BUILD
#include "../include/maps.h"

// Dummy map instances for tests
struct jmp_table_map   jmp_table;
struct panic_flag_map  panic_flag;
struct wl_map	       whitelist_map;
struct ids_flow_v4_map flow_table_v4;
struct ids_flow_v6_map flow_table_v6;
struct acl_port_map    acl_ports;
struct ipv4_drop_map   ipv4_drop;
struct ipv6_drop_map   ipv6_drop;
struct tcp_flow_map    tcp_flow;
struct udp_flow_map    udp_flow;
struct tcp6_flow_map   tcp6_flow;
struct udp6_flow_map   udp6_flow;
struct path_stats_map  path_stats;
struct icmp_allow_map  icmp_allow;

#define WL_CAP 128
struct wl_entry {
	struct wl_v6_key key;
	__u8		 val;
	int		 used;
};
static struct wl_entry wl_tab[WL_CAP];

// Mock helper functions
void*		     mock_map_value;
static void*	     mock_map_seq[8];
static int	     mock_map_idx;
static int	     use_seq;
static unsigned char mock_storage[64];
static int	     tailcall_enable;

static inline int bpf_xdp_load_bytes(struct xdp_md* ctx, int off, void* to,
				     __u32 len)
{
	if ((char*)ctx->data + off + len > (char*)ctx->data_end)
		return BPF_ERR;
	memcpy(to, (char*)ctx->data + off, len);
	return BPF_OK;
}

static void wl_reset(void)
{
	for (int i = 0; i < WL_CAP; ++i)
		wl_tab[i].used = 0;
}

static inline void* bpf_map_lookup_elem(void* map, const void* key)
{
	if (use_seq)
		return mock_map_seq[mock_map_idx++];
	if (map == &whitelist_map) {
		const struct wl_v6_key* k = key;
		for (int i = 0; i < WL_CAP; ++i)
			if (wl_tab[i].used &&
			    !memcmp(&wl_tab[i].key, k, sizeof(*k)))
				return &wl_tab[i].val;
		return NULL;
	}
	return mock_map_value;
}

static inline long bpf_map_update_elem(void* map, const void* key,
				       const void* val, __u64 flags)
{
	if (map == &whitelist_map) {
		const struct wl_v6_key* k = key;
		const __u8*		v = val;
		for (int i = 0; i < WL_CAP; ++i)
			if (wl_tab[i].used &&
			    !memcmp(&wl_tab[i].key, k, sizeof(*k))) {
				wl_tab[i].val = *v;
				return BPF_OK;
			}
		for (int i = 0; i < WL_CAP; ++i)
			if (!wl_tab[i].used) {
				wl_tab[i].key  = *k;
				wl_tab[i].val  = *v;
				wl_tab[i].used = 1;
				return BPF_OK;
			}
		return BPF_ERR;
	}
	(void)flags;
	memcpy(mock_storage, val, sizeof(mock_storage));
	mock_map_value = mock_storage;
	return BPF_OK;
}

static inline long bpf_map_delete_elem(void* map, const void* key)
{
	if (map == &whitelist_map) {
		const struct wl_v6_key* k = key;
		for (int i = 0; i < WL_CAP; ++i)
			if (wl_tab[i].used &&
			    !memcmp(&wl_tab[i].key, k, sizeof(*k))) {
				wl_tab[i].used = 0;
				return BPF_OK;
			}
		return BPF_ERR;
	}
	return BPF_OK;
}

static inline void* bpf_map_lookup_percpu_elem(void* map, const void* key,
					       __u32 cpu)
{
	(void)map;
	(void)key;
	(void)cpu;
	return NULL;
}

static inline __u64 bpf_ktime_get_ns(void)
{
	return 0ULL;
}

static inline __u16 bpf_htons(__u16 x)
{
	return (x << 8) | (x >> 8);
}

static inline __u32 bpf_htonl(__u32 x)
{
	return __builtin_bswap32(x);
}

static inline __u16 bpf_ntohs(__u16 x)
{
	return bpf_htons(x);
}

static inline __u32 bpf_ntohl(__u32 x)
{
	return bpf_htonl(x);
}

#define bpf_probe_read_kernel(dest, size, src) \
	({ \
		if (src) \
			memcpy(dest, src, size); \
		else \
			memset(dest, 0, size); \
		0; \
	})

#define bpf_tail_call(ctx, map, idx)                                          \
    do {                                                                     \
        if (tailcall_enable) {                                               \
            if ((idx) == TCP_STATE_IDX)                                      \
                return xdp_tcp_state(ctx);                                   \
            else if ((idx) == UDP_STATE_IDX)                                 \
                return xdp_udp_state(ctx);                                   \
            else if ((idx) == SURICATA_IDX)                                  \
                return xdp_suricata_gate(ctx);                               \
        }                                                                    \
    } while (0)
#define __atomic_fetch_add(ptr, val, order) ({ \
        __typeof__(*(ptr)) old = *(ptr); \
        *(ptr) += (val); \
        old; \
})
#define __ATOMIC_RELAXED 0

#ifndef BPF_PREFETCH_STUB
#define BPF_PREFETCH_STUB
static inline void
bpf_prefetch(const void* p, __u32 a,
	     __u32 b) // NOLINT(bugprone-easily-swappable-parameters)
{
	(void)p;
	(void)a;
	(void)b;
}
#endif

// Now include the XDP source with TEST_BUILD defined
#include "../src/xdp.c" // NOLINT(bugprone-suspicious-include)

// Test functions
static void test_eq32(void** state)
{
	(void)state;
	assert_int_equal(eq32(0, 0), ~0u);
	assert_int_equal(eq32(0, 1), 0);
}

static void test_mask_clr(void** state)
{
	(void)state;
	struct in6_addr a;
	memset(&a, 0xff, sizeof(a));

	mask_in6(&a, 0xffff0000);
	const __u32* w = (const __u32*)&a;
	assert_int_equal(w[0], 0xffff0000);
	assert_int_equal(w[1], 0xffff0000);
	assert_int_equal(w[2], 0xffff0000);
	assert_int_equal(w[3], 0xffff0000);

	clr_in6(&a, 0x0);
	assert_int_equal(w[0], 0x0);
	assert_int_equal(w[1], 0x0);
	assert_int_equal(w[2], 0x0);
	assert_int_equal(w[3], 0x0);
}

static void test_is_fin_rst(void** state)
{
	(void)state;
	assert_int_equal(is_fin_rst(0x01), 1); // FIN
	assert_int_equal(is_fin_rst(0x04), 1); // RST
	assert_int_equal(is_fin_rst(0x10), 0); // ACK
}

static void test_idx_v4(void** state)
{
	(void)state;
	struct flow_key k   = {.saddr = 0x01020304,
			       .daddr = 0x05060708,
			       .sport = 1,
			       .dport = 2,
			       .proto = 6};
	__u32		idx = idx_v4(&k);
	assert_true(idx < FLOW_TAB_SZ);
}

static void test_idx_v6(void** state)
{
	(void)state;
	struct bypass_v6 k = {
	    .saddr = {0}, .daddr = {0}, .sport = 1, .dport = 2, .proto = 17};
	__u32 idx = idx_v6(&k);
	assert_true(idx < FLOW_TAB_SZ);
}

static void test_is_private_ipv4(void** state)
{
	(void)state;
	assert_true(is_private_ipv4(bpf_htonl(0x0a000001)));  // 10.0.0.1
	assert_true(is_private_ipv4(bpf_htonl(0xac100001)));  // 172.16.0.1
	assert_true(is_private_ipv4(bpf_htonl(0xc0a80001)));  // 192.168.0.1
	assert_true(is_private_ipv4(bpf_htonl(0xa9fe0001)));  // 169.254.0.1
	assert_false(is_private_ipv4(bpf_htonl(0x08080808))); // 8.8.8.8
}

static void test_parse_ipv4_udp(void** state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00; // IPv4
	buf[14] = 0x45;
	buf[23] = 17;	// UDP
	buf[26] = 10;	// 10.0.0.1
	buf[27] = 0;
	buf[28] = 0;
	buf[29] = 1;
	buf[30] = 10; // 10.0.0.2
	buf[31] = 0;
	buf[32] = 0;
	buf[33] = 2;

	struct pkt p = {0};
	parse(&ctx, &p);
	assert_int_equal(p.v4, ~0u);
	assert_int_equal(p.v6, 0);
	assert_int_equal(p.udp, 0xff);
	assert_int_equal(p.sip, bpf_htonl(0x0a000001));
}

static void test_parse_ipv6_tcp(void** state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd; // IPv6
	buf[14] = 0x60; // version
	buf[20] = 6;	// TCP

	unsigned char src[16] = {0x20, 0x01, 0, 0, 0, 0, 0, 0,
				 0,    0,    0, 0, 0, 0, 0, 1};
	unsigned char dst[16] = {0x20, 0x01, 0, 0, 0, 0, 0, 0,
				 0,    0,    0, 0, 0, 0, 0, 2};
	memcpy(buf + 22, src, 16);
	memcpy(buf + 38, dst, 16);

	struct pkt p = {0};
	parse(&ctx, &p);
	assert_int_equal(p.v4, 0);
	assert_int_equal(p.v6, ~0u);
	assert_int_equal(p.udp, 0);
}

static void test_drop_ipv4_private(void** state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[26] = 10; // 10.0.0.1
	buf[27] = 0;
	buf[28] = 0;
	buf[29] = 1;

	assert_int_equal(drop_ipv4(&ctx, bpf_htons(ETH_P_IP)), 1);
}

static void test_drop_ipv6_ula(void** state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd;
	buf[14] = 0x60;
	buf[20] = 17;
	buf[22] = 0xfc; // ULA

	assert_int_equal(drop_ipv6(&ctx, bpf_htons(ETH_P_IPV6)), 1);
}

static void test_drop_ipv6_linklocal(void** state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd;
	buf[14] = 0x60;
	buf[20] = 6;
	buf[22] = 0xfe;
	buf[23] = 0x80; // fe80::

	assert_int_equal(drop_ipv6(&ctx, bpf_htons(ETH_P_IPV6)), 1);
}

static void test_xdp_wl_pass_hit(void** state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;

	static __u8 val = 1;
	use_seq		= 1;
	mock_map_idx	= 0;
	mock_map_seq[0] = &val; // whitelist hit

	assert_int_equal(xdp_wl_pass(&ctx), XDP_PASS);
	assert_int_equal(mock_map_idx, 1);
	use_seq = 0;
}

static void test_xdp_wl_pass_echo_miss(void** state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00; // IPv4
	buf[14] = 0x45;
	buf[23] = PROTO_ICMP;

	use_seq		= 1;
	mock_map_idx	= 0;
	mock_map_seq[0] = NULL; /* whitelist miss */

	assert_int_equal(xdp_wl_pass(&ctx), XDP_DROP);
	use_seq = 0;
}

static void test_xdp_wl_pass_icmp_other(void** state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00; // IPv4
	buf[14] = 0x45;
	buf[23] = PROTO_ICMP;
	buf[34] = 11; // non-echo

	use_seq		= 1;
	mock_map_idx	= 0;
	mock_map_seq[0] = NULL; /* whitelist miss */

	assert_int_equal(xdp_wl_pass(&ctx), XDP_PASS);
	use_seq = 0;
}

static void test_xdp_wl_pass_echo_hit(void** state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00; // IPv4
	buf[14] = 0x45;
	buf[23] = PROTO_ICMP;

	static __u8 val = 1;
	use_seq		= 1;
	mock_map_idx	= 0;
	mock_map_seq[0] = &val; /* whitelist hit */

	assert_int_equal(xdp_wl_pass(&ctx), XDP_PASS);
	use_seq = 0;
}

static void test_xdp_acl_ipv4_allowed(void** state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00; // IPv4
	buf[14] = 0x45;
	buf[23] = 6;	// TCP
	buf[36] = 0x00;
	buf[37] = 22;	// dport 22

	__u64 mask     = 1ull << 22;
	mock_map_value = &mask;
	assert_int_equal(xdp_acl(&ctx), XDP_PASS);
}

static void test_xdp_acl_ipv6_allowed(void** state)
{
	(void)state;
	unsigned char buf[100] = {0};
	struct xdp_md ctx      = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd; // IPv6
	buf[14] = 0x60;
	buf[20] = 17;	// UDP
	buf[56] = 0x00;
	buf[57] = 0x35; // dport 53

	__u64 mask     = 1ull << 53;
	mock_map_value = &mask;
	assert_int_equal(xdp_acl(&ctx), XDP_PASS);
}

static void test_xdp_acl_ipv4_denied(void** state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00; // IPv4
	buf[14] = 0x45;
	buf[23] = 6;
	buf[36] = 0x01;
	buf[37] = 62; // dport 62

	mock_map_value = NULL;
	assert_int_equal(xdp_acl(&ctx), XDP_DROP);
}

static void test_xdp_acl_ipv6_denied(void** state)
{
	(void)state;
	unsigned char buf[100] = {0};
	struct xdp_md ctx      = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd; // IPv6
	buf[14] = 0x60;
	buf[20] = 17;
	buf[56] = 0x00;
	buf[57] = 60; // dport 60

	mock_map_value = NULL;
	assert_int_equal(xdp_acl(&ctx), XDP_DROP);
}

static void test_xdp_acl_icmpv4_allowed(void** state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00; // IPv4
	buf[14] = 0x45;
	buf[23] = PROTO_ICMP;
	buf[34] = 11; // type
	buf[35] = 0;  // code

	__u64 mask     = 0;
	mock_map_value = &mask;
	assert_int_equal(xdp_acl(&ctx), XDP_PASS);
}

static void test_xdp_acl_icmpv4_echo_allowed(void** state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00; // IPv4
	buf[14] = 0x45;
	buf[23] = PROTO_ICMP;
	buf[34] = 8; // echo
	buf[35] = 0;

	mock_map_value = NULL;
	assert_int_equal(xdp_acl(&ctx), XDP_PASS);
}

static void test_xdp_acl_icmpv6_allowed(void** state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd; // IPv6
	buf[14] = 0x60;
	buf[20] = PROTO_ICMP6;
	buf[54] = 2; // type
	buf[55] = 0; // code

	__u64 mask6    = 1ull << 53;
	mock_map_value = &mask6;
	assert_int_equal(xdp_acl(&ctx), XDP_PASS);
}

static void test_xdp_acl_icmpv6_redirect_denied(void** state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd; // IPv6
	buf[14] = 0x60;
	buf[20] = PROTO_ICMP6;
	buf[54] = 137; // redirect
	buf[55] = 0;

	mock_map_value = NULL;
	assert_int_equal(xdp_acl(&ctx), XDP_DROP);
}

static void test_allow_ipv4_icmp_echo(void** state)
{
	(void)state;

	mock_map_value = NULL;
	assert_int_equal(allow_ipv4(PROTO_ICMP, 0), 1);
}

static void test_allow_ipv6_icmp_echo(void** state)
{
	(void)state;

	mock_map_value = NULL;
	assert_int_equal(allow_ipv6(PROTO_ICMP6, 0), 1);
}

static void test_allow_l4_port_allowed(void** state)
{
	(void)state;
	__u64 mask     = 1ull << 22;
	mock_map_value = &mask;
	assert_int_equal(allow_ipv4(PROTO_TCP, 22), 1);

	__u64 mask6    = 1ull << 53;
	mock_map_value = &mask6;
	assert_int_equal(allow_ipv6(PROTO_UDP, 53), 1);
}

static void test_allow_l4_port_denied(void** state)
{
	(void)state;
	mock_map_value = NULL;
	assert_int_equal(allow_ipv4(PROTO_TCP, 62), 0);

	mock_map_value = NULL;
	assert_int_equal(allow_ipv6(PROTO_UDP, 60), 0);
}

static void test_xdp_blacklist_ipv4_private(void** state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[26] = 10;
	buf[27] = 0;
	buf[28] = 0;
	buf[29] = 1;

	assert_int_equal(xdp_blacklist(&ctx), XDP_DROP);
}

static void test_xdp_blacklist_ipv4_public(void** state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[26] = 8;
	buf[27] = 8;
	buf[28] = 8;
	buf[29] = 8;

	assert_int_equal(xdp_blacklist(&ctx), XDP_PASS);
}

static void test_xdp_udp_state_pass(void** state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[14] = 0x45;
	buf[23] = 17;

	assert_int_equal(xdp_udp_state(&ctx), XDP_PASS);
}

static void test_xdp_tcp_state_pass(void** state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[14] = 0x45;
	buf[23] = 6;	// TCP
	buf[47] = 0x02; // SYN flag at offset 33 (ETH_HLEN + 20 + 13)

	assert_int_equal(xdp_tcp_state(&ctx), XDP_PASS);
}

static void test_xdp_udp_state_ipv6(void** state)
{
	(void)state;
	unsigned char buf[100] = {0};
	struct xdp_md ctx      = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd;
	buf[14] = 0x60;
	buf[20] = 17; // UDP

	assert_int_equal(xdp_udp_state(&ctx), XDP_PASS);
}

static void test_xdp_tcp_state_ipv6(void** state)
{
	(void)state;
	unsigned char buf[100] = {0};
	struct xdp_md ctx      = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd;
	buf[14] = 0x60;
	buf[20] = 6;	// TCP
	buf[67] = 0x02; // SYN at offset 53 (ETH_HLEN + 40 + 13)

	assert_int_equal(xdp_tcp_state(&ctx), XDP_PASS);
}

static void test_parse_l2_l3_ipv4(void** state)
{
	(void)state;
	unsigned char buf[60] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[14] = 0x45;
	buf[23] = 6;

	struct dispatch_ctx d = {0};
	parse_l2_l3(&ctx, &d);
	assert_int_equal(d.is_ipv4, ~0u);
	assert_int_equal(d.is_ipv6, 0);
	assert_int_equal(d.is_tcp, ~0u);
	assert_int_equal(d.hdr_len, 20);
}

static void test_parse_l2_l3_ipv6(void** state)
{
	(void)state;
	unsigned char buf[100] = {0};
	struct xdp_md ctx      = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd;
	buf[14] = 0x60;
	buf[20] = 17;

	struct dispatch_ctx d = {0};
	parse_l2_l3(&ctx, &d);
	assert_int_equal(d.is_ipv4, 0);
	assert_int_equal(d.is_ipv6, ~0u);
	assert_int_equal(d.is_udp, ~0u);
	assert_int_equal(d.hdr_len, 40);
}

static void test_suricata_gate_bad_ipv6(void** state)
{
	(void)state;
	unsigned char buf[60] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + 50};

	buf[12] = 0x86;
	buf[13] = 0xdd;
	buf[14] = 0x60;

	assert_int_equal(xdp_suricata_gate(&ctx), XDP_PASS);
}

static void test_suricata_gate_bad_ipv4(void** state)
{
	(void)state;
	unsigned char buf[40] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + 30};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[14] = 0x45;

	assert_int_equal(xdp_suricata_gate(&ctx), XDP_PASS);
}

static void test_panic_flag_drop(void** state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;

	static __u8 flag = 1;
	use_seq		 = 1;
	mock_map_idx	 = 0;
	mock_map_seq[0]	 = NULL;  // whitelist miss
	mock_map_seq[1]	 = &flag; // panic_flag

	assert_int_equal(xdp_wl_pass(&ctx), XDP_PASS);
	assert_int_equal(xdp_panic_flag(&ctx), XDP_DROP);
	use_seq = 0;
}

static void test_dynamic_wl(void** state)
{
	(void)state;
	wl_reset();
	__u8		 one = 1;
	struct wl_v6_key k   = {.family = AF_INET};
	for (int i = 0; i < 64; ++i) {
		__u32 ip	    = bpf_htonl(0x0a000001 + i);
		k.addr.s6_addr32[0] = ip;
		assert_int_equal(
		    bpf_map_update_elem(&whitelist_map, &k, &one, BPF_ANY),
		    BPF_OK);
	}

	unsigned char buf[64] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};
	buf[12]		      = 0x08;
	buf[13]		      = 0x00;
	__u32 ip	      = bpf_htonl(0x0a000001);
	memcpy(buf + ETH_HLEN + 12, &ip, 4);

	assert_int_equal(xdp_wl_pass(&ctx), XDP_PASS);

	static __u8 flag = 1;
	mock_map_value	 = &flag;
	assert_int_equal(bpf_map_delete_elem(&whitelist_map, &k), BPF_OK);
	xdp_wl_pass(&ctx);
	assert_int_equal(xdp_panic_flag(&ctx), XDP_DROP);

	k.addr.s6_addr32[0] = bpf_htonl(0x0a000041);
	memcpy(buf + ETH_HLEN + 12, &k.addr.s6_addr32[0], 4);
	assert_int_equal(bpf_map_update_elem(&whitelist_map, &k, &one, BPF_ANY),
			 BPF_OK);

	assert_int_equal(xdp_wl_pass(&ctx), XDP_PASS);
}

static void test_fastpath_counter(void** state)
{
	(void)state;
	unsigned char buf[60] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[14] = 0x45;
	buf[23] = 6;

	static __u64 cnt = 0;
	use_seq		 = 1;
	mock_map_idx	 = 0;
	mock_map_seq[0]	 = &cnt; // path_stats fast
	mock_map_seq[1]	 = NULL; // tcp_flow
	mock_map_seq[2]	 = NULL; // udp_flow
	mock_map_seq[3]	 = NULL; // tcp6_flow
	mock_map_seq[4]	 = NULL; // udp6_flow

	xdp_flow_fastpath(&ctx);
	assert_int_equal(cnt, 1);
	use_seq = 0;
}

static void test_slowpath_counter(void** state)
{
	(void)state;
	unsigned char buf[60] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[14] = 0x45;
	buf[23] = 6;

	static __u64 cnt = 0;
	use_seq		 = 1;
	mock_map_idx	 = 0;
	mock_map_seq[0]	 = &cnt; // path_stats slow

	xdp_proto_dispatch(&ctx);
	assert_int_equal(cnt, 1);
	use_seq = 0;
}

static void test_fastpath_tcp_fin_cleanup(void** state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[14] = 0x45;
	buf[23] = 6;
	buf[47] = 0x11; // FIN+ACK

	static __u64 cnt = 0, ts = 0;
	use_seq		= 1;
	mock_map_idx	= 0;
	mock_map_seq[0] = &cnt; // path_stats
	mock_map_seq[1] = &ts;	// tcp_flow hit
	mock_map_seq[2] = NULL; // udp_flow
	mock_map_seq[3] = NULL; // tcp6_flow
	mock_map_seq[4] = NULL; // udp6_flow
	tailcall_enable = 1;

	assert_int_equal(xdp_flow_fastpath(&ctx), XDP_PASS);
	use_seq = 0;

	unsigned char buf2[80] = {0};
	struct xdp_md ctx2 = {.data = buf2, .data_end = buf2 + sizeof(buf2)};

	buf2[12] = 0x08;
	buf2[13] = 0x00;
	buf2[14] = 0x45;
	buf2[23] = 6;
	buf2[47] = 0x10; // ACK

	use_seq		= 1;
	mock_map_idx	= 0;
	mock_map_seq[0] = &cnt; // path_stats
	mock_map_seq[1] = NULL; // tcp_flow miss
	mock_map_seq[2] = NULL;
	mock_map_seq[3] = NULL;
	mock_map_seq[4] = NULL;

	assert_int_equal(xdp_flow_fastpath(&ctx2), XDP_PASS);
	use_seq = 0;
}

static void test_fastpath_miss_drop(void** state)
{
	(void)state;
	unsigned char buf[60] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[14] = 0x45;
	buf[23] = 6;

	static __u64 cnt = 0;
	use_seq		 = 1;
	mock_map_idx	 = 0;
	mock_map_seq[0]	 = &cnt; // path_stats
	mock_map_seq[1]	 = NULL; // tcp_flow miss
	mock_map_seq[2]	 = NULL; // udp_flow miss
	mock_map_seq[3]	 = NULL; // tcp6_flow miss
	mock_map_seq[4]	 = NULL; // udp6_flow miss
	tailcall_enable	 = 1;

	assert_int_equal(xdp_flow_fastpath(&ctx), XDP_PASS);
	use_seq = 0;
}

static void test_fastpath_icmp_bypass(void** state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00; // IPv4
	buf[14] = 0x45;
	buf[23] = PROTO_ICMP;

	assert_int_equal(xdp_flow_fastpath(&ctx), XDP_PASS);
}

static void test_rl_cfg_get_default(void** state)
{
	(void)state;
	use_seq		  = 1;
	mock_map_idx	  = 0;
	mock_map_seq[0]	  = NULL;
	struct rl_cfg cfg = rl_cfg_get();
	use_seq		  = 0;
	assert_int_equal(cfg.ns, DEF_NS);
	assert_int_equal(cfg.br, DEF_BURST);
}

static void test_rl_cfg_get_override(void** state)
{
	(void)state;
	struct rl_cfg val = {.ns = 5000, .br = 50};
	use_seq		  = 1;
	mock_map_idx	  = 0;
	mock_map_seq[0]	  = &val;
	struct rl_cfg cfg = rl_cfg_get();
	use_seq		  = 0;
	assert_int_equal(cfg.ns, val.ns);
	assert_int_equal(cfg.br, val.br);
}

static void test_token_bucket_drop(void** state)
{
	(void)state;
	struct udp_key	k   = {0};
	struct rl_cfg	cfg = {.ns = 100, .br = 10};
	struct udp_meta m   = {.last_seen = 0, .tokens = 0};
	use_seq		    = 1;
	mock_map_idx	    = 0;
	mock_map_seq[0]	    = &m;
	__u32 drop	    = token_bucket_update(&k, cfg, 0);
	use_seq		    = 0;
	assert_int_equal(drop, 1);
}

static void test_make_key_ipv4(void** state)
{
	(void)state;
	struct tcp_ctx t = {.is_ipv4 = 1, .is_ipv6 = 0, .saddr = 0x01020304};
	struct ip_key  k = make_key(&t);
	const __u32*   w = (const __u32*)&k.addr;
	assert_int_equal(k.is_v6, 0);
	assert_int_equal(w[0], 0x01020304);
	assert_int_equal(w[1], 0);
	assert_int_equal(w[2], 0);
	assert_int_equal(w[3], 0);
}

static void test_udp_rl_tailcall_fail(void** state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx     = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[14] = 0x45;
	buf[23] = 17; // UDP

	static struct udp_meta m   = {.last_seen = 0, .tokens = 0};
	static __u64	       cnt = 0;
	use_seq			   = 1;
	mock_map_idx		   = 0;
	mock_map_seq[0]		   = &cnt; // path_stats
	mock_map_seq[1]		   = NULL; // tcp_flow miss
	mock_map_seq[2]		   = NULL; // udp_flow miss
	mock_map_seq[3]		   = NULL; // tcp6_flow miss
	mock_map_seq[4]		   = NULL; // udp6_flow miss
	mock_map_seq[5]		   = &m;   // udp_rl
	tailcall_enable		   = 0;	   // emulate failure

	assert_int_equal(xdp_flow_fastpath(&ctx), XDP_DROP);
	use_seq = 0;
}

int main(void)
{
	const struct CMUnitTest tests[] = {
	    cmocka_unit_test(test_eq32),
	    cmocka_unit_test(test_mask_clr),
	    cmocka_unit_test(test_is_fin_rst),
	    cmocka_unit_test(test_idx_v4),
	    cmocka_unit_test(test_idx_v6),
	    cmocka_unit_test(test_is_private_ipv4),
	    cmocka_unit_test(test_parse_ipv4_udp),
	    cmocka_unit_test(test_parse_ipv6_tcp),
	    cmocka_unit_test(test_drop_ipv4_private),
	    cmocka_unit_test(test_drop_ipv6_ula),
	    cmocka_unit_test(test_drop_ipv6_linklocal),
	    cmocka_unit_test(test_xdp_wl_pass_hit),
	    cmocka_unit_test(test_xdp_wl_pass_echo_miss),
	    cmocka_unit_test(test_xdp_wl_pass_icmp_other),
	    cmocka_unit_test(test_xdp_wl_pass_echo_hit),
	    cmocka_unit_test(test_xdp_blacklist_ipv4_private),
	    cmocka_unit_test(test_xdp_blacklist_ipv4_public),
	    cmocka_unit_test(test_xdp_acl_ipv4_allowed),
	    cmocka_unit_test(test_xdp_acl_ipv6_allowed),
	    cmocka_unit_test(test_xdp_acl_ipv4_denied),
	    cmocka_unit_test(test_xdp_acl_ipv6_denied),
	    cmocka_unit_test(test_xdp_acl_icmpv4_allowed),
	    cmocka_unit_test(test_xdp_acl_icmpv4_echo_allowed),
	    cmocka_unit_test(test_xdp_acl_icmpv6_allowed),
	    cmocka_unit_test(test_xdp_acl_icmpv6_redirect_denied),
	    cmocka_unit_test(test_allow_ipv4_icmp_echo),
	    cmocka_unit_test(test_allow_ipv6_icmp_echo),
	    cmocka_unit_test(test_allow_l4_port_allowed),
	    cmocka_unit_test(test_allow_l4_port_denied),
	    cmocka_unit_test(test_xdp_udp_state_pass),
	    cmocka_unit_test(test_xdp_tcp_state_pass),
	    cmocka_unit_test(test_xdp_udp_state_ipv6),
	    cmocka_unit_test(test_xdp_tcp_state_ipv6),
	    cmocka_unit_test(test_parse_l2_l3_ipv4),
	    cmocka_unit_test(test_parse_l2_l3_ipv6),
	    cmocka_unit_test(test_suricata_gate_bad_ipv6),
	    cmocka_unit_test(test_suricata_gate_bad_ipv4),
	    cmocka_unit_test(test_panic_flag_drop),
	    cmocka_unit_test(test_dynamic_wl),
	    cmocka_unit_test(test_fastpath_counter),
	    cmocka_unit_test(test_slowpath_counter),
	    cmocka_unit_test(test_fastpath_tcp_fin_cleanup),
	    cmocka_unit_test(test_fastpath_miss_drop),
	    cmocka_unit_test(test_fastpath_icmp_bypass),
	    cmocka_unit_test(test_rl_cfg_get_default),
	    cmocka_unit_test(test_rl_cfg_get_override),
	    cmocka_unit_test(test_token_bucket_drop),
	    cmocka_unit_test(test_make_key_ipv4),
	    cmocka_unit_test(test_udp_rl_tailcall_fail),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
