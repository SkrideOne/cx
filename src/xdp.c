#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

// SPDX-License-Identifier: GPL-2.0

#define MAP_DEF
#include "maps.h"
#undef MAP_DEF

char _license[] SEC("license") = "GPL";

#define ETH_HLEN          14
#define ETH_P_IP          0x0800
#define ETH_P_IPV6        0x86DD
#define ETH_P_IP_BE       0x0008u
#define ETH_P_IPV6_BE     0xDD86u
#define AF_INET           2
#define AF_INET6          10
#define PROTO_TCP         6
#define PROTO_UDP         17
#define IPV6_HDR_LEN      40
#define SYN_RATE_LIMIT    20
#define SYN_BURST_LIMIT   100
#define RATE_WINDOW_NS    1000000000ULL
#define TCP_STATE_IDX     8
#define UDP_STATE_IDX     9
#define SURICATA_IDX      6
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
};

struct ip_key {
    __u8        is_v6;
    __u8        pad[3];
    struct in6_addr addr;
};

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 16384);
    __type(key, struct ip_key);
    __type(value, struct rate_limit);
} tcp_rate SEC(".maps");

struct tcp_ctx {
    __u32           is_ipv4;
    __u32           is_ipv6;
    __u32           saddr;
    struct in6_addr saddr6;
    __u8            syn_only;
};

struct udp_key {
    __u8        is_v6;
    __u8        pad[3];
    struct in6_addr addr;
};

struct udp_meta {
    __u64 last_seen;
    __u32 tokens;
};

struct rl_cfg {
    __u64 ns;
    __u32 br;
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct rl_cfg);
} cfg_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 65536);
    __uint(map_flags, BPF_F_NO_COMMON_LRU);
    __type(key, struct udp_key);
    __type(value, struct udp_meta);
} udp_rl SEC(".maps");

struct pkt {
    __u32        v4, v6;
    __u32        sip;
    struct in6_addr sip6;
    __u8        udp;
};

struct flow_ctx {
    __u8               ip4, ip6, l4, tcp, udp;
    __u32               iph;
    struct flow_key           k4;
    struct ids_flow_v6_key k6;
    __u8               hit_tcp4, hit_udp4, hit_tcp6, hit_udp6;
};

struct dispatch_ctx {
    __u32               is_ipv4, is_ipv6, is_tcp, is_udp;
    __u32               hdr_len;
    __u8               l4_proto;
    struct flow_key           k4;
    struct ids_flow_v6_key k6;
};

static __always_inline __u32 eq32(__u32 a, __u32 b)
{
    __u32 xor = a ^ b;
    return -((xor-1) >> 31);
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
    __u32          h = (__u32)(s[0] ^ s[1] ^ d[0] ^ d[1]);
    h ^= ((__u32)k->sport << 16) | k->dport;
    h ^= k->proto;
    return h & (FLOW_TAB_SZ - 1);
}

SEC("xdp")
int xdp_core(struct xdp_md* ctx)
{
    bpf_tail_call(ctx, &jmp_table, 1);
    return XDP_PASS;
}

static __always_inline void* wl_lookup_v4(void* data, void* end)
{
    struct wl_u_key k = {.family = AF_INET};

    if (data + ETH_HLEN + 20 > end)
        return NULL;

    *(__u32*)k.addr = *(__be32*)(data + ETH_HLEN + 12);

    return bpf_map_lookup_elem(&wl_map, &k);
}

static __always_inline void* wl_lookup_v6(void* data, void* end)
{
    struct wl_u_key k = {.family = AF_INET6};

    if (data + ETH_HLEN + 24 > end)
        return NULL;

    __builtin_memcpy(k.addr, data + ETH_HLEN + 8, 16);

    return bpf_map_lookup_elem(&wl_map, &k);
}

SEC("xdp")
int xdp_wl_pass(struct xdp_md* ctx)
{
    void* data = (void*)(long)ctx->data;
    void* end  = (void*)(long)ctx->data_end;
    __u32 k = 0;
    __u64* v;

    if (data + ETH_HLEN > end)
        return XDP_DROP;

    __u16 proto = *(__be16*)(data + 12);

    if (proto == bpf_htons(ETH_P_IP)) {
        if (wl_lookup_v4(data, end))
            return XDP_PASS;
        goto miss;
    }

    if (proto == bpf_htons(ETH_P_IPV6)) {
        if (wl_lookup_v6(data, end))
            return XDP_PASS;
        goto miss;
    }

    return XDP_PASS;

miss:
    v = bpf_map_lookup_elem(&wl_miss, &k);
    if (v)
        __sync_fetch_and_add(v, 1);

    bpf_tail_call_static(ctx, &jmp_table, 2);
    return XDP_PASS;
}

SEC("xdp")
int xdp_panic_flag(struct xdp_md* ctx)
{
    const __u32 k   = 0;
    const __u8* v   = bpf_map_lookup_elem(&panic_flag, &k);
    __u32       drop = v ? (*v & 1u) : 0u;
    __u32       idx  = 3u + (drop << 3);
    bpf_tail_call(ctx, &jmp_table, idx);
    return drop ? XDP_DROP : XDP_PASS;
}

static __always_inline __u32 port_allowed(__u16 dp)
{
    return !!bpf_map_lookup_elem(&acl_ports, &dp);
}

static __always_inline __u32 allow_ipv4(struct xdp_md* ctx, __u16 proto)
{
    __u32 err = proto ^ bpf_htons(ETH_P_IP);
    __u8  vhl = 0, l4 = 0;
    err |= bpf_xdp_load_bytes(ctx, ETH_HLEN, &vhl, 1);
    err |= bpf_xdp_load_bytes(ctx, ETH_HLEN + 9, &l4, 1);
    __u32 ihl = (vhl & 0x0fu) << 2;

    __u16 dp = 0;
    err |= bpf_xdp_load_bytes(ctx, ETH_HLEN + ihl + 2, &dp, 2);
    dp = bpf_ntohs(dp);

    __u32 l4_ok = !(l4 ^ PROTO_TCP) | !(l4 ^ PROTO_UDP);
    return (!err) & l4_ok & port_allowed(dp);
}

static __always_inline __u32 allow_ipv6(struct xdp_md* ctx, __u16 proto)
{
    __u32 err = proto ^ bpf_htons(ETH_P_IPV6);
    __u8  nh  = 0;
    err |= bpf_xdp_load_bytes(ctx, ETH_HLEN + 6, &nh, 1);

    __u16 dp = 0;
    err |= bpf_xdp_load_bytes(ctx, ETH_HLEN + 42, &dp, 2);
    dp = bpf_ntohs(dp);

    __u32 l4_ok = !(nh ^ PROTO_TCP) | !(nh ^ PROTO_UDP);
    return (!err) & l4_ok & port_allowed(dp);
}

SEC("xdp")
int xdp_acl_dport(struct xdp_md* ctx)
{
    __u16 proto = 0;
    bpf_xdp_load_bytes(ctx, ETH_HLEN - 2, &proto, 2);

    __u32 allow = allow_ipv4(ctx, proto) | allow_ipv6(ctx, proto);
    __u32 idx   = 4u + ((allow ^ 1u) << 3);
    bpf_tail_call(ctx, &jmp_table, idx);

    return (int)XDP_DROP + (int)allow;
}

static __always_inline __u32 is_priv4(__u32 ip)
{
    return ((ip & bpf_htonl(0xff000000)) == bpf_htonl(0x0a000000)) |
           ((ip & bpf_htonl(0xfff00000)) == bpf_htonl(0xac100000)) |
           ((ip & bpf_htonl(0xffff0000)) == bpf_htonl(0xc0a80000)) |
           ((ip & bpf_htonl(0xffff0000)) == bpf_htonl(0xa9fe0000));
}

static __always_inline __u32 drop_v4(struct xdp_md* ctx, __u16 proto)
{
    if (proto != bpf_htons(ETH_P_IP))
        return 0;

    __u32 ip = 0;
    if (bpf_xdp_load_bytes(ctx, ETH_HLEN + 12, &ip, 4))
        return 1;

    __u32 bl = !!bpf_map_lookup_elem(&ip_blacklist, &ip);
    return bl | is_priv4(ip);
}

static __always_inline __u32 drop_v6(struct xdp_md* ctx, __u16 proto)
{
    if (proto != bpf_htons(ETH_P_IPV6))
        return 0;

    struct ip6_key k = {};
    if (bpf_xdp_load_bytes(ctx, ETH_HLEN + 8, &k, 16))
        return 1;

    __u8* p    = (__u8*)&k;
    __u8  ula  = ((*p & 0xfeu) == 0xfcu);
    __u8  llnk = (*p == 0xfeu) && ((p[1] & 0xc0u) == 0x80u);
    __u32 bl   = !!bpf_map_lookup_elem(&ip6_blacklist, &k);

    return bl | ula | llnk;
}

SEC("xdp")
int xdp_blacklist(struct xdp_md* ctx)
{
    __u16 proto = 0;
    bpf_xdp_load_bytes(ctx, ETH_HLEN - 2, &proto, 2);

    __u32 drop = drop_v4(ctx, proto) | drop_v6(ctx, proto);
    __u32 idx  = 5u + (drop << 3);
    bpf_tail_call(ctx, &jmp_table, idx);

    return drop ? XDP_DROP : XDP_PASS;
}

static __always_inline void parse_l2(struct xdp_md* ctx, struct flow_ctx* f)
{
    __u16 p = 0;
    bpf_xdp_load_bytes(ctx, ETH_HLEN - 2, &p, 2);
    f->ip4 = p == bpf_htons(ETH_P_IP);
    f->ip6 = p == bpf_htons(ETH_P_IPV6);
}

static __always_inline void parse_l3(struct xdp_md* ctx, struct flow_ctx* f)
{
    __u8 proto_v4 = 0, proto_v6 = 0, vhl = 0;
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 9, &proto_v4, 1);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 6, &proto_v6, 1);
    f->l4  = f->ip4 * proto_v4 + f->ip6 * proto_v6;
    f->tcp = f->l4 == PROTO_TCP;
    f->udp = f->l4 == PROTO_UDP;
    bpf_xdp_load_bytes(ctx, ETH_HLEN, &vhl, 1);
    __u32 ihl4 = ((__u32)(vhl & 0x0F) << 2);
    f->iph       = f->ip4 * ihl4 + f->ip6 * 40;
}

static __always_inline void build_keys(struct xdp_md* ctx, struct flow_ctx* f)
{
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 12, &f->k4.saddr, 4);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 16, &f->k4.daddr, 4);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + f->iph, &f->k4.sport, 2);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + f->iph + 2, &f->k4.dport, 2);
    f->k4.proto = f->l4;
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 8, f->k6.saddr, 16);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 24, f->k6.daddr, 16);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 40, &f->k6.sport, 2);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 42, &f->k6.dport, 2);
    f->k6.proto = f->l4;
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
    __u64* t4   = bpf_map_lookup_elem(&tcp_flow, &f->k4);
    f->hit_tcp4 = fresh_ts(t4, now, TCP_IDLE_NS);
    __u64* u4   = bpf_map_lookup_elem(&udp_flow, &f->k4);
    f->hit_udp4 = fresh_ts(u4, now, UDP_IDLE_NS);
}

static __always_inline void lookup_hits_v6(struct flow_ctx* f, __u64 now)
{
    __u64* t6   = bpf_map_lookup_elem(&tcp6_flow, &f->k6);
    f->hit_tcp6 = fresh_ts(t6, now, TCP_IDLE_NS);
    __u64* u6   = bpf_map_lookup_elem(&udp6_flow, &f->k6);
    f->hit_udp6 = fresh_ts(u6, now, UDP_IDLE_NS);
}

static __always_inline void lookup_hits(struct flow_ctx* f)
{
    __u64 now = bpf_ktime_get_ns();
    lookup_hits_v4(f, now);
    lookup_hits_v6(f, now);
}

static __always_inline void cleanup_fin_rst(struct xdp_md* ctx, struct flow_ctx* f)
{
    __u8 fl4 = 0, fl6 = 0;
    bpf_xdp_load_bytes(ctx, ETH_HLEN + f->iph + 13, &fl4, 1);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 53, &fl6, 1);
    __u8          fin_rst = is_fin_rst(f->ip4 * fl4 + f->ip6 * fl6) & f->tcp;
    struct flow_key k4    = f->k4;
    __u8          mask4   = -(fin_rst & f->ip4);
    k4.proto &= mask4;
    bpf_map_delete_elem(&tcp_flow, &k4);
    struct ids_flow_v6_key k6    = f->k6;
    __u8           mask6 = -(fin_rst & f->ip6);
    k6.proto &= mask6;
    bpf_map_delete_elem(&tcp6_flow, &k6);
}

static __always_inline void do_tailcall(struct xdp_md* ctx, struct flow_ctx* f)
{
    __u8 hit4_tcp = f->hit_tcp4 * f->ip4 * f->tcp;
    __u8 hit4_udp = f->hit_udp4 * f->ip4 * f->udp;
    __u8 hit6_tcp = f->hit_tcp6 * f->ip6 * f->tcp;
    __u8 hit6_udp = f->hit_udp6 * f->ip6 * f->udp;
    __u8 any_hit  = hit4_tcp | hit4_udp | hit6_tcp | hit6_udp;
    __u32 idx = hit4_tcp * TCP_STATE_IDX | hit4_udp * UDP_STATE_IDX |
                hit6_tcp * TCP_STATE_IDX | hit6_udp * UDP_STATE_IDX |
                (!any_hit) * SURICATA_IDX;
    bpf_tail_call(ctx, &jmp_table, idx);
}

SEC("xdp")
int xdp_flow_fastpath(struct xdp_md* ctx)
{
    struct flow_ctx f = {};
    parse_l2(ctx, &f);
    parse_l3(ctx, &f);
    build_keys(ctx, &f);
    lookup_hits(&f);
    cleanup_fin_rst(ctx, &f);
    do_tailcall(ctx, &f);
    return XDP_PASS;
}

static __always_inline __u32 parse_v4(struct xdp_md* ctx, struct flow_key* k)
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

static __always_inline __u32 parse_v6(struct xdp_md* ctx, struct bypass_v6* k6)
{
    __u32 err = 0;
    __u8  nh  = 0;
    LD(ETH_HLEN + 6, &nh);
    err |= bpf_xdp_load_bytes(ctx, ETH_HLEN + 8, k6->saddr, 16);
    err |= bpf_xdp_load_bytes(ctx, ETH_HLEN + 24, k6->daddr, 16);
    err |= bpf_xdp_load_bytes(ctx, ETH_HLEN + 40, &k6->sport, 2);
    err |= bpf_xdp_load_bytes(ctx, ETH_HLEN + 42, &k6->dport, 2);
    k6->proto = nh;
    k6->dir   = 0;
    return err;
}

SEC("xdp")
int xdp_suricata_gate(struct xdp_md* ctx)
{
    __u32 err   = 0;
    __u16 proto = 0;
    LD(ETH_HLEN - 2, &proto);

    struct flow_key k4 = {};
    if (!(proto ^ bpf_htons(ETH_P_IP)) && !parse_v4(ctx, &k4)) {
        __u32           idx = idx_v4(&k4);
        struct bypass_v4* v   = bpf_map_lookup_percpu_elem(&flow_table_v4, &idx, 0);
        if (v && v->saddr == k4.saddr && v->daddr == k4.daddr && v->sport == k4.sport &&
            v->dport == k4.dport && v->proto == k4.proto)
            return XDP_DROP;
    }

    struct bypass_v6 k6 = {};
    if (!(proto ^ bpf_htons(ETH_P_IPV6)) && !parse_v6(ctx, &k6)) {
        __u32           idx = idx_v6(&k6);
        struct bypass_v6* v6  = bpf_map_lookup_percpu_elem(&flow_table_v6, &idx, 0);
        if (v6 && !__builtin_memcmp(v6->saddr, k6.saddr, 16) &&
            !__builtin_memcmp(v6->daddr, k6.daddr, 16) && v6->sport == k6.sport &&
            v6->dport == k6.dport && v6->proto == k6.proto)
            return XDP_DROP;
    }

    bpf_tail_call(ctx, &jmp_table, 7);
    return XDP_PASS;
}

static __always_inline void parse_l2_l3(struct xdp_md* ctx, struct dispatch_ctx* d)
{
    __u16 eth_proto = 0;
    __u8  vhl = 0, proto_v4 = 0, proto_v6 = 0;

    bpf_xdp_load_bytes(ctx, ETH_HLEN - 2, &eth_proto, 2);
    bpf_xdp_load_bytes(ctx, ETH_HLEN, &vhl, 1);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 9, &proto_v4, 1);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 6, &proto_v6, 1);

    d->is_ipv4 = eq32(eth_proto, ETH_P_IP_BE);
    d->is_ipv6 = eq32(eth_proto, ETH_P_IPV6_BE);

    __u32 ihl   = (__u32)(vhl & 0x0Fu) << 2;
    d->hdr_len  = (ihl & d->is_ipv4) | (IPV6_HDR_LEN & d->is_ipv6);
    d->l4_proto = (proto_v4 & (__u8)d->is_ipv4) | (proto_v6 & (__u8)d->is_ipv6);

    d->is_tcp = eq32(d->l4_proto, PROTO_TCP);
    d->is_udp = eq32(d->l4_proto, PROTO_UDP);
}

static __always_inline void build_keys_dispatch(struct xdp_md* ctx, struct dispatch_ctx* d)
{
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 12, &d->k4.saddr, 4);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 16, &d->k4.daddr, 4);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + d->hdr_len, &d->k4.sport, 2);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + d->hdr_len + 2, &d->k4.dport, 2);
    d->k4.proto = d->l4_proto;

    bpf_xdp_load_bytes(ctx, ETH_HLEN + 8, d->k6.saddr, 16);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 24, d->k6.daddr, 16);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 40, &d->k6.sport, 2);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 42, &d->k6.dport, 2);
    d->k6.proto = d->l4_proto;
}

static __always_inline void update_flows(struct dispatch_ctx* d)
{
    __u64               ts     = bpf_ktime_get_ns();
    struct flow_key           k4_tcp = d->k4, k4_udp = d->k4;
    struct ids_flow_v6_key k6_tcp = d->k6, k6_udp = d->k6;

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
    struct dispatch_ctx d = {};

    parse_l2_l3(ctx, &d);
    build_keys_dispatch(ctx, &d);
    update_flows(&d);

    __u32 idx = (TCP_STATE_IDX & d.is_tcp) | (UDP_STATE_IDX & d.is_udp) |
                (INVALID_IDX & ~(d.is_tcp | d.is_udp));

    bpf_tail_call(ctx, &jmp_table, idx);
    return XDP_PASS;
}

static __always_inline void detect_ip_proto(struct xdp_md* ctx, struct tcp_ctx* t)
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

static __always_inline void load_tcp_flags(struct xdp_md* ctx, struct tcp_ctx* t)
{
    __u8 vhl = 0;
    bpf_xdp_load_bytes(ctx, ETH_HLEN, &vhl, 1);
    __u32 ihl     = ((__u32)(vhl & 0x0F) << 2);
    __u32 tcp_off = ETH_HLEN + (ihl & t->is_ipv4) + (IPV6_HDR_LEN & t->is_ipv6);
    __u8 flags    = 0;
    bpf_xdp_load_bytes(ctx, tcp_off + 13, &flags, 1);

    __u8 syn  = (flags >> 1) & 1;
    __u8 ack  = (flags >> 4) & 1;
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
    struct ip_key k = {};
    k.is_v6        = t->is_ipv6;
    k.addr         = t->saddr6;
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

static __always_inline __u32 store_rl(struct ip_key* k, __u32 add, struct rate_limit rl)
{
    __u64 now     = bpf_ktime_get_ns();
    __u64 elapsed = now - rl.window_start;
    __u32 in_window = elapsed < RATE_WINDOW_NS;

    __u64 m64 = -(__u64)in_window;
    __u32 m32 = -in_window;

    rl.window_start = (rl.window_start & m64) | (now & ~m64);
    rl.syn_count    = (rl.syn_count & m32) + add;

    __u32 exceeded = (rl.syn_count > SYN_RATE_LIMIT) | (rl.syn_count > SYN_BURST_LIMIT);
    bpf_map_update_elem(&tcp_rate, k, &rl, BPF_ANY);
    return exceeded;
}

static __always_inline __u32 check_rate_limit(struct tcp_ctx* t)
{
    __u32          check = t->syn_only;
    struct ip_key  k    = make_key(t);
    struct rate_limit rl    = load_rl(&k, bpf_ktime_get_ns());
    return store_rl(&k, check, rl) & check;
}

SEC("xdp")
int xdp_tcp_state(struct xdp_md* ctx)
{
    struct tcp_ctx t = {};
    parse_packet(ctx, &t);
    int drop = (int)(check_rate_limit(&t) & 1);
    return XDP_PASS ^ ((XDP_PASS ^ XDP_DROP) & -drop);
}

static __always_inline void parse(struct xdp_md* ctx, struct pkt* p)
{
    __u16 eth = 0;
    bpf_xdp_load_bytes(ctx, ETH_HLEN - 2, &eth, 2);
    p->v4     = eq32(eth, ETH_P_IP_BE);
    p->v6     = eq32(eth, ETH_P_IPV6_BE);
    __u8 pr4 = 0, pr6 = 0;
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 9, &pr4, 1);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 6, &pr6, 1);
    __u8 l4 = (pr4 & p->v4) | (pr6 & p->v6);
    p->udp    = eq32(l4, PROTO_UDP);
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 12, &p->sip, 4);
    p->sip &= p->v4;
    bpf_xdp_load_bytes(ctx, ETH_HLEN + 8, &p->sip6, sizeof(p->sip6));
    clr_in6(&p->sip6, p->v6);
}

static __always_inline struct rl_cfg cfg_get(void)
{
    struct rl_cfg  d   = {.ns = DEF_NS, .br = DEF_BURST};
    __u32           idx = 0;
    struct rl_cfg* ptr = bpf_map_lookup_elem(&cfg_map, &idx);
    struct rl_cfg  c   = d;
    long           rc  = bpf_probe_read_kernel(&c, sizeof(c), ptr);
    __u64           ok  = -((__s64)(rc == 0));
    c.ns           = (c.ns & ok) | (d.ns & ~ok);
    c.br           = (c.br & ok) | (d.br & ~ok);
    __u64 zns       = -((__s64)(c.ns == 0));
    __u32 zbr       = -((__s32)(c.br == 0));
    c.ns           = (c.ns & ~zns) | (d.ns & zns);
    c.br           = (c.br & ~zbr) | (d.br & zbr);
    return c;
}

static __always_inline struct udp_meta meta_ensure(struct udp_key* k, __u32 br, __u64 now)
{
    struct udp_meta init = {.last_seen = now, .tokens = br};
    bpf_map_update_elem(&udp_rl, k, &init, BPF_NOEXIST);
    struct udp_meta m = init, *ptr = bpf_map_lookup_elem(&udp_rl, k);
    long           rc = bpf_probe_read_kernel(&m, sizeof(m), ptr);
    __u64          ok = -((__s64)(rc == 0));
    m.last_seen       = (m.last_seen & ok) | (now & ~ok);
    m.tokens          = (m.tokens & ok) | (br & ~ok);
    return m;
}

static __always_inline __u32 tb_update(struct udp_key* k, struct rl_cfg c, __u64 now)
{
    struct udp_meta m    = meta_ensure(k, c.br, now);
    __u64          idle  = now - m.last_seen;
    __u64          reset = -((__s64)(idle >= TTL_NS));
    __u32          t0    = (c.br & reset) | (m.tokens & ~reset);
    __u64          add   = idle / c.ns;
    __u64          sum   = t0 + add;
    __u32          over  = -((__s64)(sum > c.br));
    __u32          tlim  = (c.br & over) | ((__u32)sum & ~over);
    __u32          has   = tlim != 0;
    __u32          drop  = (~has) & 1;
    __u32          t_after = tlim - has;
    struct udp_meta upd   = {.last_seen = now, .tokens = t_after};
    bpf_map_update_elem(&udp_rl, k, &upd, BPF_ANY);
    return drop;
}

SEC("xdp")
int xdp_udp_state(struct xdp_md* ctx)
{
    struct pkt p = {};
    parse(ctx, &p);
    struct rl_cfg  cfg = cfg_get();
    struct udp_key k   = {};
    k.is_v6           = p.v6 != 0;
    __u32*         ka   = (__u32*)&k.addr;
    const __u32*   sa   = (const __u32*)&p.sip6;
    ka[0]           = (p.sip & p.v4) | (sa[0] & p.v6);
    ka[1]           = sa[1] & p.v6;
    ka[2]           = sa[2] & p.v6;
    ka[3]           = sa[3] & p.v6;
    __u32           drop  = tb_update(&k, cfg, bpf_ktime_get_ns());
    __u32           fire  = (p.udp != 0) & drop;
    __s32           dm    = -((__s64)fire);
    unsigned int    act   = (XDP_DROP & dm) | (XDP_PASS & ~dm);
    return (int)act;
}

