#include <arpa/inet.h>
#include <bpf/bpf.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <linux/if_link.h>

/* Purpose: user-space whitelist management utility */
/* SPDX-License-Identifier: GPL-2.0 */
struct wl_v6_key {
    __u8 family;
    __u8 pad[3];
    struct in6_addr addr;
};

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <add|del> <IP>\n", prog);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        usage(argv[0]);
        return 1;
    }

    struct wl_v6_key k = {0};
    const char *cmd = argv[1];
    const char *ip  = argv[2];

    if (strchr(ip, ':')) {
        k.family = AF_INET6;
        if (inet_pton(AF_INET6, ip, &k.addr) != 1) {
            perror("inet_pton");
            return 1;
        }
    } else {
        k.family = AF_INET;
        memset(&k.addr, 0, sizeof(k.addr));
        if (inet_pton(AF_INET, ip, &((__u32*)&k.addr)[0]) != 1) {
            perror("inet_pton");
            return 1;
        }
    }

    int fd = bpf_obj_get("/sys/fs/bpf/whitelist_map");
    if (fd < 0) {
        perror("bpf_obj_get");
        return 1;
    }

    if (!strcmp(cmd, "add")) {
        __u8 one = 1;
        if (bpf_map_update_elem(fd, &k, &one, BPF_ANY)) {
            perror("update");
            return 1;
        }
    } else if (!strcmp(cmd, "del")) {
        if (bpf_map_delete_elem(fd, &k)) {
            perror("delete");
            return 1;
        }
    } else {
        usage(argv[0]);
        return 1;
    }

    return 0;
}
