# Global panic flag

To block all traffic except whitelisted addresses, update `panic_flag`:

```sh
bpftool map update name panic_flag key 0  value 1
```

Set value to `0` to disable the global drop mode.
