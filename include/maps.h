#ifndef MAPS_H_
#define MAPS_H_
// Purpose: BPF map definitions shared across modules
// Pipeline: clang-format > clang-tidy > custom lint > build > test
// Actions: define map structures and externs
// SPDX-License-Identifier: GPL-2.0

#ifndef TEST_BUILD
#include <bpf/bpf_helpers.h>
#include <linux/in6.h>
#endif

#ifndef MAP_DEF
#define MAP_EXTERN extern
#define MAP_SEC(name)
#else
#define MAP_EXTERN

#define MAP_SEC(name) SEC(name)
#endif

#define FLOW_TAB_SZ 65536

/* Core structures - cache-aligned */

/* Whitelist IPv6 key */
struct wl_v6_key {
	__u8		family;
	__u8		pad[3];
	struct in6_addr addr;
} __attribute__((aligned(64)));

struct flow_key {
	__u32 saddr, daddr;
	__u16 sport, dport;
	__u8  proto;
	__u8  pad[3]; /* Explicit padding */
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

struct icmp_key {
	__u8 family;
	__u8 type;
	__u8 code;
};

#ifndef TEST_BUILD
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

/* Global Suricata bypass flag */
struct global_bypass_map {
        __uint(type, BPF_MAP_TYPE_ARRAY);
        __uint(max_entries, 1);
        __uint(pinning, LIBBPF_PIN_BY_NAME);
        __uint(map_flags, BPF_F_RDONLY_PROG);
        __type(key, __u32);
        __type(value, __u8);
};
MAP_EXTERN struct global_bypass_map global_bypass MAP_SEC(".maps");

/* Whitelist - dynamic, managed from user space */
struct wl_map {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 64);
	__type(key, struct wl_v6_key);
	__type(value, __u8);
};
MAP_EXTERN struct wl_map whitelist_map MAP_SEC(".maps");

/* Flow tables - percpu arrays for speed */

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
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
};
MAP_EXTERN struct acl_port_map acl_ports MAP_SEC(".maps");

struct icmp_allow_map {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 32);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, struct icmp_key);
	__type(value, __u8);
};
MAP_EXTERN struct icmp_allow_map icmp_allow MAP_SEC(".maps");

/* Blacklists */
struct ipv4_drop_map {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 4096);
	__type(key, __u32);
	__type(value, __u8);
};
MAP_EXTERN struct ipv4_drop_map ipv4_drop MAP_SEC(".maps");

struct ipv6_drop_map {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 4096);
	__type(key, struct ip6_key);
	__type(value, __u8);
};
MAP_EXTERN struct ipv6_drop_map ipv6_drop MAP_SEC(".maps");

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

/* Fast/slow path counters */
struct path_stats_map {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 2);
	__type(key, __u32);
	__type(value, __u64);
};
MAP_EXTERN struct path_stats_map path_stats MAP_SEC(".maps");

#else  // TEST_BUILD

// For tests, we just need dummy map declarations

// Dummy map structures for tests
struct jmp_table_map {
};
struct panic_flag_map {
};
struct wl_map {
};
struct ids_flow_v4_map {
};
struct ids_flow_v6_map {
};
struct acl_port_map {
};
struct ipv4_drop_map {
};
struct ipv6_drop_map {
};
struct tcp_flow_map {
};
struct udp_flow_map {
};
struct tcp6_flow_map {
};
struct udp6_flow_map {
};
struct icmp_allow_map {
};
struct global_bypass_map {
};

// Dummy map instances
MAP_EXTERN struct jmp_table_map	  jmp_table;
MAP_EXTERN struct panic_flag_map  panic_flag;
MAP_EXTERN struct global_bypass_map global_bypass;
MAP_EXTERN struct wl_map	  whitelist_map;
MAP_EXTERN struct ids_flow_v4_map flow_table_v4;
MAP_EXTERN struct ids_flow_v6_map flow_table_v6;
MAP_EXTERN struct acl_port_map	  acl_ports;
MAP_EXTERN struct ipv4_drop_map	  ipv4_drop;
MAP_EXTERN struct ipv6_drop_map	  ipv6_drop;
MAP_EXTERN struct tcp_flow_map	  tcp_flow;
MAP_EXTERN struct udp_flow_map	  udp_flow;
MAP_EXTERN struct icmp_allow_map  icmp_allow;
MAP_EXTERN struct tcp6_flow_map	  tcp6_flow;
MAP_EXTERN struct udp6_flow_map	  udp6_flow;
struct path_stats_map {
};
MAP_EXTERN struct path_stats_map path_stats;

#endif // TEST_BUILD

#endif
