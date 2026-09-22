---
name: rh-deploy
description: >-
  Deploy Macintosh Plus apps via the HelloMacintosh CLI (cloned by
  `make bootstrap`). Use when the user asks to run, stream, push, rebuild,
  or deploy a Mac guest, or mentions rh / RetroHarness / hellomacintosh.
---

# Deploy with HelloMacintosh

The resident loader and host CLI are **not** in this RetroSlack tree. They live in
**[HelloMacintosh](https://github.com/kApp-Oliver/HelloMacintosh)** — clone it
separately (or let `make bootstrap` clone it into `HelloMacintosh/`).

```bash
make bootstrap
make mac
./scripts/hellomacintosh ping
./scripts/hellomacintosh run build/mac/RetroSlack.bin
# then start NSE on the same serial
./build/host/nse --serial /dev/cu.usbserial-XXXX --baud 19200 --verbose
```

Snow:

```bash
./scripts/hellomacintosh --serial tcp://127.0.0.1:1984 ping
make deploy SNOW=1
```

- Wrapper: `./scripts/hellomacintosh` → `$HELLOMACINTOSH_DIR/scripts/hellomacintosh`
- Serial: `$HM_SERIAL`, else `/dev/cu.usbserial*`, or `--serial tcp://127.0.0.1:1984` for Snow
- Arduino passthrough: in the HelloMacintosh checkout, `./scripts/flash-bridge.sh`
- Guest CMake: `add_harness_guest` — see [mac-harness-guest](../mac-harness-guest/SKILL.md)

This repo builds **NSE** (`make host` → `build/host/nse`) and **RetroSlack.bin**.
Do not expect `./scripts/rh`; use `./scripts/hellomacintosh`.

Stop NSE before `hellomacintosh run` / `stop` (one process owns USB). After the
guest starts, run NSE on the same serial for Slack.
