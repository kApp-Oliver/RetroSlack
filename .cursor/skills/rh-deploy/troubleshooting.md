# `hellomacintosh` troubleshooting

The CLI lives in [HelloMacintosh](https://github.com/kApp-Oliver/HelloMacintosh).
`make bootstrap` clones it; `./scripts/hellomacintosh` wraps it.

| Symptom | Cause / fix |
|---------|-------------|
| `hellomacintosh ping` times out | Loader not running/frontmost; wrong `--serial`; NSE on the port; Arduino DTR reset — retry once |
| `no serial device` | Pass `--serial /dev/cu.usbmodem…` or `--serial tcp://127.0.0.1:1984` |
| `HelloMacintosh not found` | `make bootstrap`, or `git clone https://github.com/kApp-Oliver/HelloMacintosh.git HelloMacintosh`, or set `HELLOMACINTOSH_DIR` |
| `unknown method` | Old loader. In the HelloMacintosh checkout: `./scripts/hellomacintosh update build/mac/HelloMacintosh.bin` |
| Guest launches but `stop` / `ping` fail | App not linked with `add_harness_guest`, or it never calls `WaitNextEvent` |
| `file busy` / `stop the guest` | Guest still frontmost. `stop`, then `run` |
| Two CLI processes / Resource busy | Only one host process may open the USB device |

`./scripts/rh` does not exist. Use `./scripts/hellomacintosh`.
