// Purpose: Unit tests for XDP program
// Pipeline: clang-format > clang-tidy > custom lint > build > test
// Actions: validate IPv4/IPv6 parsing and ACL logic
// SPDX-License-Identifier: GPL-2.0

// Test environment - we don't include vmlinux.h in tests
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
#include <cmocka.h>

// Basic type definitions for tests
typedef uint8_t  __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef int32_t  __s32;
typedef int64_t  __s64;

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
#define AF_INET 2
#define AF_INET6 10

// BPF map type definitions for tests
#define BPF_MAP_TYPE_HASH 1
#define BPF_MAP_TYPE_ARRAY 2
#define BPF_MAP_TYPE_PROG_ARRAY 3
#define BPF_MAP_TYPE_PERCPU_HASH 5
#define BPF_MAP_TYPE_PERCPU_ARRAY 6
#define BPF_MAP_TYPE_LRU_HASH 9

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
	void *data;
	void *data_end;
};

// Test-specific in6_addr structure
struct in6_addr {
	union {
		__u8  u6_addr8[16];
		__u16 u6_addr16[8];
		__u32 u6_addr32[4];
	} in6_u;
#define s6_addr in6_u.u6_addr8
};

// Mock helper functions
void *mock_map_value;
static void *mock_map_seq[8];
static int mock_map_idx;
static int use_seq;
static unsigned char mock_storage[64];

static inline int bpf_xdp_load_bytes(struct xdp_md *ctx, int off, void *to, __u32 len)
{
        if ((char *)ctx->data + off + len > (char *)ctx->data_end)
                return BPF_ERR;
	memcpy(to, (char *)ctx->data + off, len);
        return BPF_OK;
}

static inline void *bpf_map_lookup_elem(void *map, const void *key)
{
        (void)map;
        (void)key;
        if (use_seq)
                return mock_map_seq[mock_map_idx++];
        return mock_map_value;
}

static inline long bpf_map_update_elem(void *map, const void *key, const void *val, __u64 flags)
{
	(void)map;
	(void)key;
	(void)flags;
	memcpy(mock_storage, val, sizeof(mock_storage));
	mock_map_value = mock_storage;
        return BPF_OK;
}

static inline long bpf_map_delete_elem(void *map, const void *key)
{
	(void)map;
	(void)key;
        return BPF_OK;
}

static inline void *bpf_map_lookup_percpu_elem(void *map, const void *key, __u32 cpu)
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

#define bpf_tail_call(ctx, map, idx) do { } while(0)
#define __atomic_fetch_add(ptr, val, order) ({ \
	__typeof__(*(ptr)) old = *(ptr); \
	*(ptr) += (val); \
	old; \
})
#define __ATOMIC_RELAXED 0

// Now include the XDP source with TEST_BUILD defined
#define TEST_BUILD
#include "../src/xdp.c"

// Test functions
static void test_eq32(void **state)
{
	(void)state;
	assert_int_equal(eq32(0, 0), ~0u);
	assert_int_equal(eq32(0, 1), 0);
}

static void test_mask_clr(void **state)
{
	(void)state;
	struct in6_addr a;
	memset(&a, 0xff, sizeof(a));

	mask_in6(&a, 0xffff0000);
	const __u32 *w = (const __u32 *)&a;
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

static void test_is_fin_rst(void **state)
{
	(void)state;
	assert_int_equal(is_fin_rst(0x01), 1); // FIN
	assert_int_equal(is_fin_rst(0x04), 1); // RST
	assert_int_equal(is_fin_rst(0x10), 0); // ACK
}

static void test_idx_v4(void **state)
{
	(void)state;
	struct flow_key k = {
		.saddr = 0x01020304,
		.daddr = 0x05060708,
		.sport = 1,
		.dport = 2,
		.proto = 6
	};
	__u32 idx = idx_v4(&k);
	assert_true(idx < FLOW_TAB_SZ);
}

static void test_idx_v6(void **state)
{
	(void)state;
	struct bypass_v6 k = {
		.saddr = {0},
		.daddr = {0},
		.sport = 1,
		.dport = 2,
		.proto = 17
	};
	__u32 idx = idx_v6(&k);
	assert_true(idx < FLOW_TAB_SZ);
}

static void test_is_priv4(void **state)
{
	(void)state;
	assert_true(is_priv4(bpf_htonl(0x0a000001)));  // 10.0.0.1
	assert_true(is_priv4(bpf_htonl(0xac100001)));  // 172.16.0.1
	assert_true(is_priv4(bpf_htonl(0xc0a80001)));  // 192.168.0.1
	assert_true(is_priv4(bpf_htonl(0xa9fe0001)));  // 169.254.0.1
	assert_false(is_priv4(bpf_htonl(0x08080808))); // 8.8.8.8
}

static void test_parse_ipv4_udp(void **state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00; // IPv4
	buf[14] = 0x45;
	buf[23] = 17;   // UDP
	buf[26] = 10;   // 10.0.0.1
	buf[27] = 0;
	buf[28] = 0;
	buf[29] = 1;
	buf[30] = 10;   // 10.0.0.2
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

static void test_parse_ipv6_tcp(void **state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd; // IPv6
	buf[14] = 0x60; // version
	buf[20] = 6;    // TCP

	unsigned char src[16] = {0x20, 0x01, 0, 0, 0, 0, 0, 0,
	                         0, 0, 0, 0, 0, 0, 0, 1};
	unsigned char dst[16] = {0x20, 0x01, 0, 0, 0, 0, 0, 0,
	                         0, 0, 0, 0, 0, 0, 0, 2};
	memcpy(buf + 22, src, 16);
	memcpy(buf + 38, dst, 16);

	struct pkt p = {0};
	parse(&ctx, &p);
	assert_int_equal(p.v4, 0);
	assert_int_equal(p.v6, ~0u);
	assert_int_equal(p.udp, 0);
}

static void test_drop_v4_private(void **state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[26] = 10;   // 10.0.0.1
	buf[27] = 0;
	buf[28] = 0;
	buf[29] = 1;

	assert_int_equal(drop_v4(&ctx, bpf_htons(ETH_P_IP)), 1);
}

static void test_drop_v6_ula(void **state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd;
	buf[14] = 0x60;
	buf[20] = 17;
	buf[22] = 0xfc; // ULA

	assert_int_equal(drop_v6(&ctx, bpf_htons(ETH_P_IPV6)), 1);
}

static void test_drop_v6_linklocal(void **state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd;
	buf[14] = 0x60;
	buf[20] = 6;
	buf[22] = 0xfe;
	buf[23] = 0x80; // fe80::

	assert_int_equal(drop_v6(&ctx, bpf_htons(ETH_P_IPV6)), 1);
}

static void test_xdp_wl_pass_hit(void **state)
{
        (void)state;
        unsigned char buf[64] = {0};
        struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

        buf[12] = 0x08;
        buf[13] = 0x00;

        __u8 val = 1;
        use_seq = 1;
        mock_map_idx = 0;
        mock_map_seq[0] = &val;  // wl_lookup_v4
        mock_map_seq[1] = NULL;  // wl_lookup_v6

        assert_int_equal(xdp_wl_pass(&ctx), XDP_PASS);
        assert_int_equal(mock_map_idx, 2);
        use_seq = 0;
}

static void test_xdp_wl_pass_miss(void **state)
{
        (void)state;
        unsigned char buf[64] = {0};
        struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

        buf[12] = 0x08;
        buf[13] = 0x00;

        __u64 cnt = 0;
        use_seq = 1;
        mock_map_idx = 0;
        mock_map_seq[0] = NULL;   // wl_lookup_v4
        mock_map_seq[1] = NULL;   // wl_lookup_v6
        mock_map_seq[2] = &cnt;   // wl_miss

        assert_int_equal(xdp_wl_pass(&ctx), XDP_PASS);
        assert_int_equal(cnt, 1);
        assert_int_equal(mock_map_idx, 3);
        use_seq = 0;
}

static void test_xdp_acl_ipv4_allowed(void **state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00; // IPv4
	buf[14] = 0x45;
	buf[23] = 6;    // TCP
	buf[36] = 0x00;
	buf[37] = 0x50; // dport 80

	int allow = 1;
	mock_map_value = &allow;
	assert_int_equal(xdp_acl_dport(&ctx), XDP_PASS);
}

static void test_xdp_acl_ipv6_allowed(void **state)
{
	(void)state;
	unsigned char buf[100] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd; // IPv6
	buf[14] = 0x60;
	buf[20] = 17;   // UDP
	buf[54] = 0x00;
	buf[55] = 0x35; // dport 53

	int allow = 1;
	mock_map_value = &allow;
	assert_int_equal(xdp_acl_dport(&ctx), XDP_PASS);
}

static void test_xdp_acl_ipv4_denied(void **state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00; // IPv4
	buf[14] = 0x45;
	buf[23] = 6;
	buf[36] = 0x01;
	buf[37] = 0xbb; // dport 443

	mock_map_value = NULL;
	assert_int_equal(xdp_acl_dport(&ctx), XDP_DROP);
}

static void test_xdp_acl_ipv6_denied(void **state)
{
	(void)state;
	unsigned char buf[100] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd; // IPv6
	buf[14] = 0x60;
	buf[20] = 17;
	buf[54] = 0x12;
	buf[55] = 0x34; // dport 0x1234

	mock_map_value = NULL;
	assert_int_equal(xdp_acl_dport(&ctx), XDP_DROP);
}

static void test_xdp_blacklist_ipv4_private(void **state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[26] = 10;
	buf[27] = 0;
	buf[28] = 0;
	buf[29] = 1;

	assert_int_equal(xdp_blacklist(&ctx), XDP_DROP);
}

static void test_xdp_blacklist_ipv4_public(void **state)
{
	(void)state;
	unsigned char buf[64] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[26] = 8;
	buf[27] = 8;
	buf[28] = 8;
	buf[29] = 8;

	assert_int_equal(xdp_blacklist(&ctx), XDP_PASS);
}

static void test_xdp_udp_state_pass(void **state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[14] = 0x45;
	buf[23] = 17;

	assert_int_equal(xdp_udp_state(&ctx), XDP_PASS);
}

static void test_xdp_tcp_state_pass(void **state)
{
	(void)state;
	unsigned char buf[80] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x08;
	buf[13] = 0x00;
	buf[14] = 0x45;
	buf[23] = 6;    // TCP
	buf[47] = 0x02; // SYN flag at offset 33 (ETH_HLEN + 20 + 13)

	assert_int_equal(xdp_tcp_state(&ctx), XDP_PASS);
}

static void test_xdp_udp_state_ipv6(void **state)
{
	(void)state;
	unsigned char buf[100] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd;
	buf[14] = 0x60;
	buf[20] = 17; // UDP

	assert_int_equal(xdp_udp_state(&ctx), XDP_PASS);
}

static void test_xdp_tcp_state_ipv6(void **state)
{
	(void)state;
	unsigned char buf[100] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

	buf[12] = 0x86;
	buf[13] = 0xdd;
	buf[14] = 0x60;
	buf[20] = 6;    // TCP
	buf[67] = 0x02; // SYN at offset 53 (ETH_HLEN + 40 + 13)

	assert_int_equal(xdp_tcp_state(&ctx), XDP_PASS);
}

static void test_parse_l2_l3_ipv4(void **state)
{
	(void)state;
	unsigned char buf[60] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

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

static void test_parse_l2_l3_ipv6(void **state)
{
	(void)state;
	unsigned char buf[100] = {0};
	struct xdp_md ctx = {.data = buf, .data_end = buf + sizeof(buf)};

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

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_eq32),
		cmocka_unit_test(test_mask_clr),
		cmocka_unit_test(test_is_fin_rst),
		cmocka_unit_test(test_idx_v4),
		cmocka_unit_test(test_idx_v6),
		cmocka_unit_test(test_is_priv4),
		cmocka_unit_test(test_parse_ipv4_udp),
		cmocka_unit_test(test_parse_ipv6_tcp),
		cmocka_unit_test(test_drop_v4_private),
		cmocka_unit_test(test_drop_v6_ula),
                cmocka_unit_test(test_drop_v6_linklocal),
                cmocka_unit_test(test_xdp_wl_pass_hit),
                cmocka_unit_test(test_xdp_wl_pass_miss),
                cmocka_unit_test(test_xdp_blacklist_ipv4_private),
		cmocka_unit_test(test_xdp_blacklist_ipv4_public),
		cmocka_unit_test(test_xdp_acl_ipv4_allowed),
		cmocka_unit_test(test_xdp_acl_ipv6_allowed),
		cmocka_unit_test(test_xdp_acl_ipv4_denied),
		cmocka_unit_test(test_xdp_acl_ipv6_denied),
		cmocka_unit_test(test_xdp_udp_state_pass),
		cmocka_unit_test(test_xdp_tcp_state_pass),
		cmocka_unit_test(test_xdp_udp_state_ipv6),
		cmocka_unit_test(test_xdp_tcp_state_ipv6),
		cmocka_unit_test(test_parse_l2_l3_ipv4),
		cmocka_unit_test(test_parse_l2_l3_ipv6),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
