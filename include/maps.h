#ifndef MAPS_H_
#define MAPS_H_
// SPDX-License-Identifier: GPL-2.0

#include <bpf/bpf_helpers.h>

#ifndef MAP_DEF
#define MAP_EXTERN extern
#define MAP_SEC(name)
#else
#define MAP_EXTERN
#define MAP_SEC(name) SEC(name)
#endif

/* Core structures - cache-aligned */
struct wl_u_key {
    __u32 family;
    __u8  addr[16];
} __attribute__((aligned(32)));

struct flow_key {
    __u32 saddr, daddr;
    __u16 sport, dport;
    __u8  proto;
    __u8  pad[3];  /* Explicit padding */
};

struct bypass_v4 {
    __u32 saddr, daddr;
    __u16 sport, dport;
    __u8  proto, dir;
} __attribute__((packed));

struct bypass_v6 {
    __u8  saddr[16], daddr[16];
    __u16 sport, dport;
    __u8  proto, dir;
} __attribute__((packed));

struct ip6_key {
    __u8 addr[16];
};

struct ids_flow_v6_key {
    __u8  saddr[16], daddr[16];
    __u16 sport, dport;
    __u8  proto;
};

/* Jump table - small, hot */
struct jmp_table_map {
    __uint(type, BPF_MAP_TYPE_PROG_ARRAY);
    __uint(max_entries, 16);
    __type(key, __u32);
    __type(value, __u32);
};
MAP_EXTERN struct jmp_table_map jmp_table MAP_SEC(".maps");

/* Panic flag - single entry array */
struct panic_flag_map {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u8);
};
MAP_EXTERN struct panic_flag_map panic_flag MAP_SEC(".maps");

/* Whitelist miss counter - percpu for no contention */
struct wl_miss_map {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
};
MAP_EXTERN struct wl_miss_map wl_miss MAP_SEC(".maps");

/* Whitelist - optimized with zero seed */
struct wl_map {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 64);
    __uint(map_flags, BPF_F_RDONLY_PROG | BPF_F_NO_PREALLOC | BPF_F_ZERO_SEED);
    __type(key, struct wl_u_key);
    __type(value, __u8);
};
MAP_EXTERN struct wl_map wl_map MAP_SEC(".maps");

/* Flow tables - percpu arrays for speed */
#define FLOW_TAB_SZ 65536

struct ids_flow_v4_map {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, FLOW_TAB_SZ);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __type(key, __u32);
    __type(value, struct bypass_v4);
};
MAP_EXTERN struct ids_flow_v4_map flow_table_v4 MAP_SEC(".maps");

struct ids_flow_v6_map {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, FLOW_TAB_SZ);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __type(key, __u32);
    __type(value, struct bypass_v6);
};
MAP_EXTERN struct ids_flow_v6_map flow_table_v6 MAP_SEC(".maps");

/* ACL ports */
struct acl_port_map {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 32);
    __uint(map_flags, BPF_F_NO_PREALLOC);
    __type(key, __u16);
    __type(value, __u8);
};
MAP_EXTERN struct acl_port_map acl_ports MAP_SEC(".maps");

/* Blacklists */
struct ip_blacklist_map {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key, __u32);
    __type(value, __u8);
};
MAP_EXTERN struct ip_blacklist_map ip_blacklist MAP_SEC(".maps");

struct ip6_blacklist_map {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key, struct ip6_key);
    __type(value, __u8);
};
MAP_EXTERN struct ip6_blacklist_map ip6_blacklist MAP_SEC(".maps");

/* Flow tracking - LRU for auto-eviction */
struct tcp_flow_map {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 32768);
    __type(key, struct flow_key);
    __type(value, __u64);
};
MAP_EXTERN struct tcp_flow_map tcp_flow MAP_SEC(".maps");

struct udp_flow_map {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 32768);
    __type(key, struct flow_key);
    __type(value, __u64);
};
MAP_EXTERN struct udp_flow_map udp_flow MAP_SEC(".maps");

struct tcp6_flow_map {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 32768);
    __type(key, struct ids_flow_v6_key);
    __type(value, __u64);
};
MAP_EXTERN struct tcp6_flow_map tcp6_flow MAP_SEC(".maps");

struct udp6_flow_map {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 1024);
    __type(key, struct ids_flow_v6_key);
    __type(value, __u64);
};
MAP_EXTERN struct udp6_flow_map udp6_flow MAP_SEC(".maps");

#endif
