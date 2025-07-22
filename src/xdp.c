#ifdef TEST_BUILD
#else
#include "vmlinux.h"
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#endif
#include <stdbool.h>

#ifdef __clang_analyzer__
#ifndef BPF_PREFETCH_STUB
#define BPF_PREFETCH_STUB
static inline void bpf_prefetch(const void* p, __u32 a, __u32 b)
{
	(void)p;
	(void)a;
	(void)b;
}
#endif
#endif

/* Forward declarations for tail-call targets */
struct xdp_md;
static int xdp_suricata_gate(struct xdp_md* ctx);
static int xdp_state(struct xdp_md* ctx);

// Purpose: XDP packet filtering logic
// Pipeline: clang-format > clang-tidy > custom lint > build > test
// Actions: parse packets and enforce ACL/flow rules
// SPDX-License-Identifier: GPL-2.0

#define MAP_DEF
#include "maps.h"
#undef MAP_DEF

char _license[] SEC("license") = "GPL";

static __always_inline __u32 parse_ipv4(struct xdp_md* ctx, struct flow_key* k);
static __always_inline __u32 parse_ipv6(struct xdp_md*	  ctx,
					struct bypass_v6* k6);

#define RET_OK 0
#define RET_ERR 1

#define ETH_HLEN          14
#define ETH_P_IP          0x0800
#define ETH_P_IPV6        0x86DD
#define ETH_P_IP_BE       0x0008u
#define ETH_P_IPV6_BE     0xDD86u
#define AF_INET           2
#define AF_INET6          10
#define PROTO_TCP         6
#define PROTO_UDP         17
#define PROTO_ICMP        1
#define PROTO_ICMP6       58
#define IPV6_HDR_LEN      40
#define SYN_RATE_LIMIT    20
#define SYN_BURST_LIMIT   100
#define RATE_WINDOW_NS    1000000000ULL
#define STATE_IDX         8
#define SURICATA_IDX      6
/* jmp_table order: panic -> state -> suricata */
#define PANIC_IDX         1
#define FAST_CNT_IDX      0
#define SLOW_CNT_IDX      1
#define INVALID_IDX       255
#define INVALID_PROTO     255
#define TCP_IDLE_NS       (15ULL * 1000000000ULL)
#define UDP_IDLE_NS       ( 5ULL * 1000000000ULL)
#define TTL_NS            5000000000ULL
#define DEF_NS            1000000ULL
#define DEF_BURST         100
#define LD(off, ptr)      ({ err |= bpf_xdp_load_bytes(ctx, (off), (ptr), sizeof(*(ptr))); })

struct rate_limit {
	__u64 window_start;
	__u32 syn_count;
} __attribute__((aligned(64)));

struct ip_key {
	__u8		is_v6;
	__u8		pad[3];
	struct in6_addr addr;
} __attribute__((aligned(64)));

struct {
	__uint(type, BPF_MAP_TYPE_LRU_PERCPU_HASH);
	__uint(max_entries, 128);
	__type(key, struct ip_key);
	__type(value, struct rate_limit);
} tcp_rate SEC(".maps");

struct tcp_ctx {
	__u32		is_ipv4;
	__u32		is_ipv6;
	__u32		saddr;
	struct in6_addr saddr6;
	__u8		syn_only;
};

struct udp_key {
	__u8		is_v6;
	__u8		pad[3];
	struct in6_addr addr;
};

struct udp_meta {
	__u64 last_seen;
	__u32 tokens;
} __attribute__((aligned(64)));

struct rl_cfg {
	__u64 ns;
	__u32 br;
} __attribute__((aligned(64)));

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct rl_cfg);
} cfg_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_PERCPU_HASH);
	__uint(max_entries, 128);
	__type(key, struct udp_key);
	__type(value, struct udp_meta);
} udp_rl SEC(".maps");

struct pkt {
	__u32		v4, v6;
	__u32		sip;
	struct in6_addr sip6;
	__u8		udp;
};

struct flow_ctx {
	__u8		       is_ipv4, is_ipv6, l4_proto, is_tcp, is_udp;
	__u32		       hdr_len;
	struct flow_key	       key_v4;
	struct ids_flow_v6_key key_v6;
	__u8		       hit_tcp_v4, hit_udp_v4, hit_tcp_v6, hit_udp_v6;
};

struct dispatch_ctx {
	__u32		       is_ipv4, is_ipv6, is_tcp, is_udp;
	__u32		       hdr_len;
	__u8		       l4_proto;
	struct flow_key	       key_v4;
	struct ids_flow_v6_key key_v6;
};
static __always_inline struct rl_cfg rl_cfg_get(void);
static __always_inline __u32	     token_bucket_update(struct udp_key* k,
							 struct rl_cfg c, __u64 now);

static __always_inline __u32 eq32(__u32 a, __u32 b)
{
	__u32 xor = a ^ b;
	return -((xor - 1) >> 31);
}

static __always_inline void mask_in6(struct in6_addr* a, __u32 keep)
{
	__u32* w = (__u32*)a;
	w[0] &= keep;
	w[1] &= keep;
	w[2] &= keep;
	w[3] &= keep;
}

static __always_inline void clr_in6(struct in6_addr* a, __u32 m)
{
	__u32* w = (__u32*)a;
	w[0] &= m;
	w[1] &= m;
	w[2] &= m;
	w[3] &= m;
}

static __always_inline __u32 is_fin_rst(__u8 f)
{
	return (f & 0x05u) != 0;
}

static __always_inline __u32 idx_v4(const struct flow_key* k)
{
	__u32 h = k->saddr ^ k->daddr;
	h ^= ((__u32)k->sport << 16) | k->dport;
	h ^= k->proto;
	return h & (FLOW_TAB_SZ - 1);
}

static __always_inline __u32 idx_v6(const struct bypass_v6* k)
{
	const __u64* s = (const __u64*)k->saddr;
	const __u64* d = (const __u64*)k->daddr;
	__u32	     h = (__u32)(s[0] ^ s[1] ^ d[0] ^ d[1]);
	h ^= ((__u32)k->sport << 16) | k->dport;
	h ^= k->proto;
	return h & (FLOW_TAB_SZ - 1);
}

static __always_inline void count_fast(void)
{
	__u32  k = FAST_CNT_IDX;
	__u64* v = bpf_map_lookup_elem(&path_stats, &k);

	if (v)
		__atomic_fetch_add(v, 1, __ATOMIC_RELAXED);
}

static __always_inline void count_slow(void)
{
	__u32  k = SLOW_CNT_IDX;
	__u64* v = bpf_map_lookup_elem(&path_stats, &k);

	if (v)
		__atomic_fetch_add(v, 1, __ATOMIC_RELAXED);
}

SEC("xdp")
int xdp_wl_pass(struct xdp_md* ctx)
{
	bpf_prefetch(ctx->data + ETH_HLEN, 0, 0);
	/* 1. Ethernet proto */
       __u16 eth = 0;
       __u32 err = bpf_xdp_load_bytes(ctx, ETH_HLEN - 2, &eth, 2);

	__u32 v4 = eq32(eth, ETH_P_IP_BE);
	__u32 v6 = eq32(eth, ETH_P_IPV6_BE);

	/* 2. src address */
	struct wl_v6_key k4 = {.family = AF_INET};
	struct wl_v6_key k6 = {.family = AF_INET6};
       bpf_xdp_load_bytes(ctx, ETH_HLEN + 12, &k4.addr, 4);
       err |= bpf_xdp_load_bytes(ctx, ETH_HLEN + 8, &k6.addr, 16) & -v6;

	/* 3. lookup */
       __u32 hit = (v4 && bpf_map_lookup_elem(&whitelist_map, &k4)) ||
                   (v6 && bpf_map_lookup_elem(&whitelist_map, &k6));

	__u8 p4 = 0, p6 = 0, vhl = 0, type = 0;
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 9, &p4, 1);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 6, &p6, 1);
	bpf_xdp_load_bytes(ctx, ETH_HLEN, &vhl, 1);

	__u8  l4      = (p4 & v4) | (p6 & v6);
	__u32 icmp4   = v4 & eq32(l4, PROTO_ICMP);
	__u32 icmp6   = v6 & eq32(l4, PROTO_ICMP6);
	__u32 is_icmp = icmp4 | icmp6;
	__u32 ihl     = (vhl & 0x0Fu) << 2;
	__u32 off     = ETH_HLEN + (ihl & v4) + (IPV6_HDR_LEN & v6);
	bpf_xdp_load_bytes(ctx, (int)off, &type, 1);

       __u32 echo4 = icmp4 & (eq32(type, 0) | eq32(type, 8));
       __u32 echo6 = icmp6 & (eq32(type, 128) | eq32(type, 129));
       __u32 drop  = (!hit) & is_icmp & (echo4 | echo6);
       drop |= err != 0;
       __u32 call = !(hit | drop);
       if (call)
               bpf_tail_call(ctx, &jmp_table, PANIC_IDX);

       __u32 res = XDP_PASS ^ ((XDP_PASS ^ XDP_DROP) & -drop);
       return res ^ ((res ^ XDP_PASS) & -hit);
}

SEC("xdp")
int xdp_panic_flag(struct xdp_md* ctx)
{
	(void)ctx;
	const __u32 k = 0;
	const __u8* v = bpf_map_lookup_elem(&panic_flag, &k);
        __u32 val = v ? *v : 0;
        __u32 active = val == 1;
       return XDP_PASS ^ ((XDP_PASS ^ XDP_DROP) & -active);
}

static __always_inline __u32 allow_l4(__u8 family, __u8 proto, __u16 port,
                                      __u64 bm)
{
       (void)family;
       __u32 ok  = (proto == PROTO_TCP) | (proto == PROTO_UDP);
       __u32 bit = ((bm >> (port & 63)) & 1u) & (port < 64);
       return ok & bit;
}

SEC("xdp")
int xdp_acl(struct xdp_md* ctx)
{
	bpf_prefetch(ctx->data + ETH_HLEN, 0, 0);
	__u16 proto = 0;
	bpf_xdp_load_bytes(ctx, ETH_HLEN - 2, &proto, 2);

	__u8 pr4 = 0, pr6 = 0, vhl = 0;
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 9, &pr4, 1);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 6, &pr6, 1);
	bpf_xdp_load_bytes(ctx, ETH_HLEN, &vhl, 1);

	__u32 is_v4 = eq32(proto, ETH_P_IP_BE);
	__u32 is_v6 = eq32(proto, ETH_P_IPV6_BE);

	__u8  l4     = (pr4 & is_v4) | (pr6 & is_v6);
	__u8  family = AF_INET * !!is_v4 + AF_INET6 * !!is_v6;
	__u32 ihl    = (vhl & 0x0Fu) << 2;
	__u32 off    = ETH_HLEN + (ihl & is_v4) + (IPV6_HDR_LEN & is_v6);

	__u16 dp = 0;
	bpf_xdp_load_bytes(ctx, (int)(off + 2), &dp, 2);
	dp = bpf_ntohs(dp);

	__u32	     key = 0;
	const __u64* m	 = bpf_map_lookup_elem(&acl_ports, &key);
	__u64	     bm	 = m ? *m : 0;

       __u32 allow = allow_l4(family, l4, dp, bm);

	__u32 is_icmp =
	    (is_v4 & eq32(l4, PROTO_ICMP)) | (is_v6 & eq32(l4, PROTO_ICMP6));
	__u8 type = 0, code = 0;
	bpf_xdp_load_bytes(ctx, (int)off, &type, 1);
	bpf_xdp_load_bytes(ctx, (int)(off + 1), &code, 1);

	struct icmp_key k = {family, type, code};
	__u32 allowed	  = is_icmp & !!bpf_map_lookup_elem(&icmp_allow, &k);

	allow |= allowed;
	return XDP_DROP + allow;
}

static __always_inline __u32 is_private_ipv4(__u32 ip)
{
	__u32 a =
	    (__u32)((ip & bpf_htonl(0xff000000)) == bpf_htonl(0x0a000000));
	__u32 b =
	    (__u32)((ip & bpf_htonl(0xfff00000)) == bpf_htonl(0xac100000));
	__u32 c =
	    (__u32)((ip & bpf_htonl(0xffff0000)) == bpf_htonl(0xc0a80000));
	__u32 d =
	    (__u32)((ip & bpf_htonl(0xffff0000)) == bpf_htonl(0xa9fe0000));
        return a | b | c | d;
}

static __always_inline __u32 bl_ipv4_hit(struct xdp_md* ctx, __u16 proto)
{
        __u32 is_v4 = proto == bpf_htons(ETH_P_IP);
        __u32 ip    = 0;
        __u32 err   = bpf_xdp_load_bytes(ctx, ETH_HLEN + 12, &ip, 4) & -is_v4;

        __u32 bl  = !!bpf_map_lookup_elem(&ipv4_drop, &ip);
        __u32 prv = is_private_ipv4(ip);

        return (bl | prv) & is_v4 & !err;
}

static __always_inline __u32 bl_ipv6_hit(struct xdp_md* ctx, __u16 proto)
{
        __u32 is_v6 = proto == bpf_htons(ETH_P_IPV6);
        struct ip6_key k = {};
        __u32 err = bpf_xdp_load_bytes(ctx, ETH_HLEN + 8, &k, 16) & -is_v6;

        __u8* p = (__u8*)&k;
        __u32 ula  = (*p & 0xfeu) == 0xfcu;
        __u32 llnk = (*p == 0xfeu) & ((p[1] & 0xc0u) == 0x80u);
        __u32 bl   = !!bpf_map_lookup_elem(&ipv6_drop, &k);

        return (bl | ula | llnk) & is_v6 & !err;
}

SEC("xdp")
int xdp_blacklist(struct xdp_md* ctx)
{
	__u16 proto = 0;
	bpf_xdp_load_bytes(ctx, ETH_HLEN - 2, &proto, 2);

       __u32 hit4 = bl_ipv4_hit(ctx, proto);
       __u32 hit6 = bl_ipv6_hit(ctx, proto);
       __u32 hit  = hit4 | hit6;

       struct flow_key k4 = {};
        if (hit4 && !parse_ipv4(ctx, &k4))
                bpf_map_delete_elem(&flow_table_v4, &k4);

       struct bypass_v6 k6 = {};
        if (hit6 && !parse_ipv6(ctx, &k6))
                bpf_map_delete_elem(&flow_table_v6, &k6);

       return XDP_PASS ^ ((XDP_PASS ^ XDP_DROP) & -hit);
}

static __always_inline void parse_l2(struct xdp_md* ctx, struct flow_ctx* f)
{
	__u16 p = 0;
	bpf_xdp_load_bytes(ctx, ETH_HLEN - 2, &p, 2);
	f->is_ipv4 = p == bpf_htons(ETH_P_IP);
	f->is_ipv6 = p == bpf_htons(ETH_P_IPV6);
}

static __always_inline void parse_l3(struct xdp_md* ctx, struct flow_ctx* f)
{
	__u8 proto_v4 = 0, proto_v6 = 0, vhl = 0;
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 9, &proto_v4, 1);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 6, &proto_v6, 1);
	f->l4_proto = f->is_ipv4 * proto_v4 + f->is_ipv6 * proto_v6;
	f->is_tcp   = f->l4_proto == PROTO_TCP;
	f->is_udp   = f->l4_proto == PROTO_UDP;
	bpf_xdp_load_bytes(ctx, ETH_HLEN, &vhl, 1);
	__u32 ihl4 = ((__u32)(vhl & 0x0F) << 2);
	f->hdr_len = f->is_ipv4 * ihl4 + f->is_ipv6 * 40;
}

static __always_inline void build_keys(struct xdp_md* ctx, struct flow_ctx* f)
{
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 12, &f->key_v4.saddr, 4);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 16, &f->key_v4.daddr, 4);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + f->hdr_len, &f->key_v4.sport, 2);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + f->hdr_len + 2, &f->key_v4.dport, 2);
	f->key_v4.proto = f->l4_proto;
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 8, f->key_v6.saddr, 16);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 24, f->key_v6.daddr, 16);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 40, &f->key_v6.sport, 2);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 42, &f->key_v6.dport, 2);
	f->key_v6.proto = f->l4_proto;
}

static __always_inline __u8 fresh_ts(__u64* ts, __u64 now, __u64 idle)
{
	__u64 v = 0;
	bpf_probe_read_kernel(&v, sizeof(v), ts);
	__u8 has = !!ts;
	__u8 exp = has & ((now - v) > idle);
	return has & (exp ^ 1);
}

static __always_inline void lookup_hits_v4(struct flow_ctx* f, __u64 now)
{
	__u64* t4     = bpf_map_lookup_elem(&tcp_flow, &f->key_v4);
	f->hit_tcp_v4 = fresh_ts(t4, now, TCP_IDLE_NS);
	__u64* u4     = bpf_map_lookup_elem(&udp_flow, &f->key_v4);
	f->hit_udp_v4 = fresh_ts(u4, now, UDP_IDLE_NS);
}

static __always_inline void lookup_hits_v6(struct flow_ctx* f, __u64 now)
{
	__u64* t6     = bpf_map_lookup_elem(&tcp6_flow, &f->key_v6);
	f->hit_tcp_v6 = fresh_ts(t6, now, TCP_IDLE_NS);
	__u64* u6     = bpf_map_lookup_elem(&udp6_flow, &f->key_v6);
	f->hit_udp_v6 = fresh_ts(u6, now, UDP_IDLE_NS);
}

static __always_inline void lookup_hits(struct flow_ctx* f)
{
	__u64 now = bpf_ktime_get_ns();
	lookup_hits_v4(f, now);
	lookup_hits_v6(f, now);
}

static __always_inline void cleanup_fin_rst(struct xdp_md*   ctx,
					    struct flow_ctx* f)
{
	__u8 fl4 = 0, fl6 = 0;
	bpf_xdp_load_bytes(ctx, ETH_HLEN + f->hdr_len + 13, &fl4, 1);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 53, &fl6, 1);
	__u8 fin_rst =
	    is_fin_rst(f->is_ipv4 * fl4 + f->is_ipv6 * fl6) & f->is_tcp;
	struct flow_key k4    = f->key_v4;
	__u8		mask4 = -(fin_rst & f->is_ipv4);
	k4.proto &= mask4;
	bpf_map_delete_elem(&tcp_flow, &k4);
	struct ids_flow_v6_key k6    = f->key_v6;
	__u8		       mask6 = -(fin_rst & f->is_ipv6);
	k6.proto &= mask6;
	bpf_map_delete_elem(&tcp6_flow, &k6);
}

static __always_inline int do_tailcall(struct xdp_md* ctx, struct flow_ctx* f)
{
	__u8  hit4_tcp = f->hit_tcp_v4 * f->is_ipv4 * f->is_tcp;
	__u8  hit4_udp = f->hit_udp_v4 * f->is_ipv4 * f->is_udp;
	__u8  hit6_tcp = f->hit_tcp_v6 * f->is_ipv6 * f->is_tcp;
	__u8  hit6_udp = f->hit_udp_v6 * f->is_ipv6 * f->is_udp;
	__u8  hit_any  = hit4_tcp | hit4_udp | hit6_tcp | hit6_udp;
	__u32 idx      = hit_any ? STATE_IDX : SURICATA_IDX;
	(void)hit_any;
	bpf_tail_call(ctx, &jmp_table, idx);
	(void)ctx;
	(void)idx;
	return -1;
}

SEC("xdp")
int xdp_flow_fastpath(struct xdp_md* ctx)
{
	count_fast();
	struct flow_ctx f = {};
	parse_l2(ctx, &f);
       parse_l3(ctx, &f);
       __u32 icmp =
           eq32(f.l4_proto, PROTO_ICMP) | eq32(f.l4_proto, PROTO_ICMP6);
	build_keys(ctx, &f);
	lookup_hits(&f);
	cleanup_fin_rst(ctx, &f);
	do_tailcall(ctx, &f); /* HIT -> TCP/UDP-state or SURICATA */
	__u32 drop = 0;
	if (f.is_udp) {
		struct rl_cfg  cfg = rl_cfg_get();
		struct udp_key k   = {.is_v6 = f.is_ipv6};
		__u32*	       ka  = (__u32*)&k.addr;
		const __u32*   sa6 = (const __u32*)f.key_v6.saddr;
		ka[0] = (f.key_v4.saddr & -f.is_ipv4) | (sa6[0] & -f.is_ipv6);
		ka[1] = sa6[1] & -f.is_ipv6;
		ka[2] = sa6[2] & -f.is_ipv6;
		ka[3] = sa6[3] & -f.is_ipv6;
		drop  = token_bucket_update(&k, cfg, bpf_ktime_get_ns());
	}
       __u32 res = XDP_PASS ^ ((XDP_PASS ^ XDP_DROP) & -drop);
       return res ^ ((res ^ XDP_PASS) & -icmp);
}

static __always_inline __u32 parse_ipv4(struct xdp_md* ctx, struct flow_key* k)
{
	__u32 err = 0;
	__u8  vhl = 0, l4 = 0;
	LD(ETH_HLEN, &vhl);
	LD(ETH_HLEN + 9, &l4);
	__u32 ihl = (vhl & 0x0F) << 2;
	LD(ETH_HLEN + 12, &k->saddr);
	LD(ETH_HLEN + 16, &k->daddr);
	LD(ETH_HLEN + ihl, &k->sport);
	LD(ETH_HLEN + ihl + 2, &k->dport);
	k->proto = l4;
	return err;
}

static __always_inline __u32 parse_ipv6(struct xdp_md*	  ctx,
					struct bypass_v6* k6)
{
	__u32 err = 0;
	__u8  nh  = 0;
	LD(ETH_HLEN + 6, &nh);
	err |= bpf_xdp_load_bytes(ctx, ETH_HLEN + 8, k6->saddr, 16);
	err |= bpf_xdp_load_bytes(ctx, ETH_HLEN + 24, k6->daddr, 16);
	err |= bpf_xdp_load_bytes(ctx, ETH_HLEN + 40, &k6->sport, 2);
	err |= bpf_xdp_load_bytes(ctx, ETH_HLEN + 42, &k6->dport, 2);
	k6->proto = nh;
	k6->dir	  = 0;
	return err;
}

/* DEAD_CODE_START */
static __always_inline int match_bypass_ipv4(const struct bypass_v4* v,
                                             const struct flow_key*  k)
{
        return v && v->saddr == k->saddr && v->daddr == k->daddr &&
               v->sport == k->sport && v->dport == k->dport &&
               v->proto == k->proto;
}

static __always_inline int match_bypass_ipv6(const struct bypass_v6* v,
                                             const struct bypass_v6* k)
{
        return v && !__builtin_memcmp(v->saddr, k->saddr, 16) &&
               !__builtin_memcmp(v->daddr, k->daddr, 16) &&
               v->sport == k->sport && v->dport == k->dport &&
               v->proto == k->proto;
}
/* DEAD_CODE_END */ // REMOVE_IN_NEXT_REFRACTOR


static __always_inline __u32 bl_ipv4_suricata(struct xdp_md* ctx, __u32 is_v4)
{
        struct flow_key k = {};
        __u32 ok  = !parse_ipv4(ctx, &k);
        __u32 hit = !!bpf_map_lookup_elem(&flow_table_v4, &k);
        return (is_v4 & ok) & (hit ^ 1);
}

static __always_inline __u32 bl_ipv6_suricata(struct xdp_md* ctx, __u32 is_v6)
{
        struct bypass_v6 k = {};
        __u32 ok  = !parse_ipv6(ctx, &k);
        __u32 hit = !!bpf_map_lookup_elem(&flow_table_v6, &k);
        return (is_v6 & ok) & (hit ^ 1);
}

SEC("xdp")
int xdp_suricata_gate(struct xdp_md* ctx)
{
	__u32	    idx	   = 0;
	const __u8* bypass = bpf_map_lookup_elem(&global_bypass, &idx);
        __u32 skip = bypass ? *bypass == 1 : 0;

	__u16 proto = 0;
	__u32 drop  = 0;

	bpf_xdp_load_bytes(ctx, ETH_HLEN - 2, &proto, 2);

	__u32 v4 = !(proto ^ bpf_htons(ETH_P_IP));
	__u32 v6 = !(proto ^ bpf_htons(ETH_P_IPV6));

       drop |= bl_ipv4_suricata(ctx, v4);
       drop |= bl_ipv6_suricata(ctx, v6);

        __u32 res = XDP_PASS ^ ((XDP_PASS ^ XDP_DROP) & -drop);
        return res ^ ((res ^ XDP_PASS) & -skip);
}

static __always_inline void parse_l2_l3(struct xdp_md*	     ctx,
					struct dispatch_ctx* d)
{
	__u16 eth_proto = 0;
	__u8  vhl = 0, proto_v4 = 0, proto_v6 = 0;

	bpf_xdp_load_bytes(ctx, ETH_HLEN - 2, &eth_proto, 2);
	bpf_xdp_load_bytes(ctx, ETH_HLEN, &vhl, 1);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 9, &proto_v4, 1);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 6, &proto_v6, 1);

	d->is_ipv4 = eq32(eth_proto, ETH_P_IP_BE);
	d->is_ipv6 = eq32(eth_proto, ETH_P_IPV6_BE);

	__u32 ihl  = (__u32)(vhl & 0x0Fu) << 2;
	d->hdr_len = (ihl & d->is_ipv4) | (IPV6_HDR_LEN & d->is_ipv6);
	d->l4_proto =
	    (proto_v4 & (__u8)d->is_ipv4) | (proto_v6 & (__u8)d->is_ipv6);

	d->is_tcp = eq32(d->l4_proto, PROTO_TCP);
	d->is_udp = eq32(d->l4_proto, PROTO_UDP);
}

static __always_inline void build_keys_dispatch(struct xdp_md*	     ctx,
						struct dispatch_ctx* d)
{
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 12, &d->key_v4.saddr, 4);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 16, &d->key_v4.daddr, 4);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + d->hdr_len, &d->key_v4.sport, 2);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + d->hdr_len + 2, &d->key_v4.dport, 2);
	d->key_v4.proto = d->l4_proto;

	bpf_xdp_load_bytes(ctx, ETH_HLEN + 8, d->key_v6.saddr, 16);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 24, d->key_v6.daddr, 16);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 40, &d->key_v6.sport, 2);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 42, &d->key_v6.dport, 2);
	d->key_v6.proto = d->l4_proto;
}

static __always_inline void update_flows(struct dispatch_ctx* d)
{
	__u64		       ts     = bpf_ktime_get_ns();
	struct flow_key	       k4_tcp = d->key_v4, k4_udp = d->key_v4;
	struct ids_flow_v6_key k6_tcp = d->key_v6, k6_udp = d->key_v6;

	__u8 m4t = (__u8)(d->is_ipv4 & d->is_tcp);
	__u8 m4u = (__u8)(d->is_ipv4 & d->is_udp);
	__u8 m6t = (__u8)(d->is_ipv6 & d->is_tcp);
	__u8 m6u = (__u8)(d->is_ipv6 & d->is_udp);

	k4_tcp.proto = (d->l4_proto & m4t) | (INVALID_PROTO & ~m4t);
	k4_udp.proto = (d->l4_proto & m4u) | (INVALID_PROTO & ~m4u);
	k6_tcp.proto = (d->l4_proto & m6t) | (INVALID_PROTO & ~m6t);
	k6_udp.proto = (d->l4_proto & m6u) | (INVALID_PROTO & ~m6u);

	bpf_map_update_elem(&tcp_flow, &k4_tcp, &ts, BPF_ANY);
	bpf_map_update_elem(&udp_flow, &k4_udp, &ts, BPF_ANY);
	bpf_map_update_elem(&tcp6_flow, &k6_tcp, &ts, BPF_ANY);
	bpf_map_update_elem(&udp6_flow, &k6_udp, &ts, BPF_ANY);
}

SEC("xdp")
int xdp_proto_dispatch(struct xdp_md* ctx)
{
	count_slow();
	struct dispatch_ctx d = {};

	parse_l2_l3(ctx, &d);
	build_keys_dispatch(ctx, &d);
	update_flows(&d);

	bpf_tail_call(ctx, &jmp_table,
		      (STATE_IDX & (d.is_tcp | d.is_udp)) |
			  (INVALID_IDX & ~(d.is_tcp | d.is_udp)));
	return XDP_PASS;
}

static __always_inline void detect_ip_proto(struct xdp_md*  ctx,
					    struct tcp_ctx* t)
{
	__u16 eth_proto = 0;
	bpf_xdp_load_bytes(ctx, ETH_HLEN - 2, &eth_proto, 2);
	t->is_ipv4 = eq32(eth_proto, ETH_P_IP_BE);
	t->is_ipv6 = eq32(eth_proto, ETH_P_IPV6_BE);
}

static __always_inline void load_src_addr(struct xdp_md* ctx, struct tcp_ctx* t)
{
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 12, &t->saddr, 4);
	t->saddr &= t->is_ipv4;
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 8, &t->saddr6, sizeof(t->saddr6));
	mask_in6(&t->saddr6, -t->is_ipv6);
}

static __always_inline void load_tcp_flags(struct xdp_md*  ctx,
					   struct tcp_ctx* t)
{
	__u8 vhl = 0;
	bpf_xdp_load_bytes(ctx, ETH_HLEN, &vhl, 1);
	__u32 ihl = ((__u32)(vhl & 0x0F) << 2);
	__u32 tcp_off =
	    ETH_HLEN + (ihl & t->is_ipv4) + (IPV6_HDR_LEN & t->is_ipv6);
	__u8 flags = 0;
	bpf_xdp_load_bytes(ctx, (int)(tcp_off + 13), &flags, 1);

	__u8 syn    = (flags >> 1) & 1;
	__u8 ack    = (flags >> 4) & 1;
	t->syn_only = syn & (~ack);
}

static __always_inline void parse_packet(struct xdp_md* ctx, struct tcp_ctx* t)
{
	detect_ip_proto(ctx, t);
	load_src_addr(ctx, t);
	load_tcp_flags(ctx, t);
}

static __always_inline struct ip_key make_key(const struct tcp_ctx* t)
{
	struct ip_key k = {.is_v6 = t->is_ipv6};
	__builtin_memcpy(&k.addr, &t->saddr6, sizeof(k.addr));
	((__u32*)&k.addr)[0] |= t->saddr;
	return k;
}

static __always_inline struct rate_limit load_rl(struct ip_key* k, __u64 now)
{
	struct rate_limit init = {.window_start = now, .syn_count = 0};
	bpf_map_update_elem(&tcp_rate, k, &init, BPF_NOEXIST);

	struct rate_limit* p  = bpf_map_lookup_elem(&tcp_rate, k);
	struct rate_limit  rl = {};
	bpf_probe_read_kernel(&rl, sizeof(rl), p);
	return rl;
}

static __always_inline __u32 store_rl(struct ip_key* k, __u32 add,
				      struct rate_limit rl)
{
	__u64 now	= bpf_ktime_get_ns();
	__u64 elapsed	= now - rl.window_start;
	__u32 in_window = elapsed < RATE_WINDOW_NS;

	__u64 m64 = -(__u64)in_window;
	__u32 m32 = -in_window;

	rl.window_start = (rl.window_start & m64) | (now & ~m64);
	rl.syn_count	= (rl.syn_count & m32) + add;

	__u32 exceeded =
	    (rl.syn_count > SYN_RATE_LIMIT) | (rl.syn_count > SYN_BURST_LIMIT);
	bpf_map_update_elem(&tcp_rate, k, &rl, BPF_ANY);
	return exceeded;
}

static __always_inline __u32 check_rate_limit(struct tcp_ctx* t)
{
	__u32		  check = t->syn_only;
	struct ip_key	  k	= make_key(t);
	struct rate_limit rl	= load_rl(&k, bpf_ktime_get_ns());
	return store_rl(&k, check, rl) & check;
}

static __always_inline __u32 tcp_state_drop(struct xdp_md* ctx)
{
	struct tcp_ctx t = {};
	parse_packet(ctx, &t);
	return check_rate_limit(&t) & 1u;
}

static __always_inline void parse(struct xdp_md* ctx, struct pkt* p)
{
	__u16 eth = 0;
	bpf_xdp_load_bytes(ctx, ETH_HLEN - 2, &eth, 2);
	p->v4	 = eq32(eth, ETH_P_IP_BE);
	p->v6	 = eq32(eth, ETH_P_IPV6_BE);
	__u8 pr4 = 0, pr6 = 0;
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 9, &pr4, 1);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 6, &pr6, 1);
	__u8 l4 = (pr4 & p->v4) | (pr6 & p->v6);
	p->udp	= eq32(l4, PROTO_UDP);
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 12, &p->sip, 4);
	p->sip &= p->v4;
	bpf_xdp_load_bytes(ctx, ETH_HLEN + 8, &p->sip6, sizeof(p->sip6));
	clr_in6(&p->sip6, p->v6);
}

static __always_inline struct rl_cfg rl_cfg_get(void)
{
	struct rl_cfg  d   = {.ns = DEF_NS, .br = DEF_BURST};
	__u32	       idx = 0;
	struct rl_cfg* ptr = bpf_map_lookup_elem(&cfg_map, &idx);
	struct rl_cfg  c   = d;
	long	       rc  = bpf_probe_read_kernel(&c, sizeof(c), ptr);
	__u64	       ok  = -((__s64)(rc == 0));
	c.ns		   = (c.ns & ok) | (d.ns & ~ok);
	c.br		   = (c.br & ok) | (d.br & ~ok);
	__u64 zns	   = -((__s64)(c.ns == 0));
	__u32 zbr	   = -((__s32)(c.br == 0));
	c.ns		   = (c.ns & ~zns) | (d.ns & zns);
	c.br		   = (c.br & ~zbr) | (d.br & zbr);
	return c;
}

static __always_inline struct udp_meta meta_ensure(struct udp_key* k, __u32 br,
						   __u64 now)
{
	struct udp_meta init = {.last_seen = now, .tokens = br};
	bpf_map_update_elem(&udp_rl, k, &init, BPF_NOEXIST);
	struct udp_meta m = init, *ptr = bpf_map_lookup_elem(&udp_rl, k);
	long		rc = bpf_probe_read_kernel(&m, sizeof(m), ptr);
	__u64		ok = -((__s64)(rc == 0));
	m.last_seen	   = (m.last_seen & ok) | (now & ~ok);
	m.tokens	   = (m.tokens & ok) | (br & ~ok);
	return m;
}

static __always_inline __u32 token_bucket_update(struct udp_key* k,
						 struct rl_cfg c, __u64 now)
{
	struct udp_meta m	= meta_ensure(k, c.br, now);
	__u64		idle	= now - m.last_seen;
	__u64		reset	= -((__s64)(idle >= TTL_NS));
	__u32		t0	= (c.br & reset) | (m.tokens & ~reset);
	__u64		add	= idle / c.ns;
	__u64		sum	= t0 + add;
	__u32		over	= -((__s64)(sum > c.br));
	__u32		tlim	= (c.br & over) | ((__u32)sum & ~over);
	__u32		has	= tlim != 0;
	__u32		drop	= (~has) & 1;
	__u32		t_after = tlim - has;
	struct udp_meta upd	= {.last_seen = now, .tokens = t_after};
	bpf_map_update_elem(&udp_rl, k, &upd, BPF_ANY);
	return drop;
}
static __always_inline __u32 udp_state_drop(struct xdp_md* ctx)
{
	struct pkt p = {};
	parse(ctx, &p);
	struct rl_cfg  cfg = rl_cfg_get();
	struct udp_key k   = {};
	k.is_v6		   = p.v6 != 0;
	__u32*	     ka	   = (__u32*)&k.addr;
	const __u32* sa	   = (const __u32*)&p.sip6;
	ka[0]		   = (p.sip & p.v4) | (sa[0] & p.v6);
	ka[1]		   = sa[1] & p.v6;
	ka[2]		   = sa[2] & p.v6;
	ka[3]		   = sa[3] & p.v6;
	__u32 drop	   = token_bucket_update(&k, cfg, bpf_ktime_get_ns());
	return (p.udp != 0) & drop;
}

SEC("xdp")
int xdp_state(struct xdp_md* ctx)
{
	__u32 drop = tcp_state_drop(ctx) | udp_state_drop(ctx);
	return XDP_PASS ^ ((XDP_PASS ^ XDP_DROP) & -drop);
}
