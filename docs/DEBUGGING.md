# Debugging

## Desktop NSE (Cursor / LLDB)

1. `make host`
2. In Cursor: **Run and Debug** → **Debug NSE** (see `.vscode/launch.json`)
3. Or: `lldb -- build/host/nse --serial tcp://127.0.0.1:1984 --verbose`

Breakpoints in `host/nse/*.c` and `protocol/rhttp_codec.c` work normally.

Verbose mode dumps frame headers and HTTP meta (not Slack tokens).

Deploying guests over serial is `./scripts/hellomacintosh` after `make bootstrap`. Do not run NSE and that CLI on the same port at once.

## Protocol unit tests

```bash
make test-protocol
```

## Mac guest (Snow)

- Compile with `RHTTP_DEBUG` to log on the **printer** port (`.BOut`).
- In Snow: enable printer port TCP/PTY bridge and `nc`/`socat` that port, or use Snow’s serial terminal.
- Snow’s built-in debugger: breakpoints, traps, memory (assembly-level).
- Retro68 `*.flt.gdb` + `m68k-apple-macos-addr2line` for symbolicated addresses after crashes.

There is no full source-level GDB for Retro68 apps inside the emulator.

## Arduino

- PlatformIO / Arduino serial monitor on the USB CDC port.
- Match golden tests from `make test-protocol` behaviorally (PING/PONG, sample REQ).

## Real Plus

- Prefer on-screen errors in RetroSlack.
- Optional second cable on printer port for `RHTTP_DEBUG` logs (AppleTalk off).
- Arduino status LED / USB monitor for NSE-side failures.
