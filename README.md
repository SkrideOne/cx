# Global panic flag

To block all traffic except whitelisted addresses, update `panic_flag`:

```sh
bpftool map update name panic_flag key 0  value 1
```

Set value to `0` to disable the global drop mode.

## Fast/slow path counters

Two counters in `path_stats` track packet routing:

* index `0` — packets handled in `xdp_flow_fastpath`
* index `1` — packets processed via `xdp_proto_dispatch`

Read them with:

```sh
bpftool map dump name path_stats
```
