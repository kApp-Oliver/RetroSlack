# RetroSlack

Classic Macintosh Plus Slack client. The Mac speaks **HTTP-over-serial (RHTTP)**; networking, TLS, and Slack live on the **macOS host** (desktop NSE). Guest Mac binaries are identical in Snow and on hardware (stock Serial Manager only). Slack tokens never leave the host.

| Environment | Path to the Plus | Slack |
|-------------|------------------|-------|
| **Development** | [Snow](https://github.com/twvd/snow) modem TCP bridge (`tcp://127.0.0.1:1984`) | desktop **NSE** (`libcurl` + `http://rs/` cache) |
| **Hardware** | macOS USB-serial → **Arduino** (byte copy) → RS-422 → Plus **Mini-DIN-8** modem port | same desktop **NSE** on that USB serial device |

```
macOS  NSE / hellomacintosh
  USB serial @ 19200 8N1
    Arduino (passthrough)
      RS-422 conversion
        Macintosh Plus modem port (Mini-DIN-8)
```

The Arduino is not on Ethernet. It is a USB–UART bridge; the MacBook talks Slack, the Plus talks RHTTP over RS-422. The Mac only requests the compact rows it intends to render.

**Demo:** [Slack on a 1986 Macintosh Plus (LinkedIn)](https://www.linkedin.com/posts/oliver-bates-603376aa_slack-retro-macintoshplus-ugcPost-7504822192052912128-VNl6). Serial deploy uses **[HelloMacintosh](https://github.com/kApp-Oliver/HelloMacintosh)**.

## HelloMacintosh (required for serial deploy)

This repository is the Slack client. Pushing a rebuilt app over the modem port is a **separate project**:

**[HelloMacintosh](https://github.com/kApp-Oliver/HelloMacintosh)** — resident loader on the Plus + `hellomacintosh` host CLI + Arduino USB–serial bridge.

Clone it yourself (sibling directory or into `HelloMacintosh/` here) or let bootstrap do it. It is **not** vendored and is **not** a git submodule; override the checkout with `HELLOMACINTOSH_DIR` or the clone URL with `HELLOMACINTOSH_REPO`.

```bash
# optional, if you prefer a sibling clone:
git clone https://github.com/kApp-Oliver/HelloMacintosh.git ../HelloMacintosh
export HELLOMACINTOSH_DIR="$PWD/../HelloMacintosh"
```

## Quick start

1. Place a legally obtained Macintosh Plus ROM at `ROMS/Mac-Plus.ROM` (gitignored; not needed for a real Plus).
2. Copy `.env.example` → `.env` and set `SLACK_USER_TOKEN` or `SLACK_BOT_TOKEN`. Never commit `.env`.
3. Bootstrap toolchain, clone HelloMacintosh, and build the desktop NSE + CLI:

```bash
make bootstrap   # Homebrew, Retro68, Snow, HelloMacintosh clone, host tools
make mac
```

### Emulator (Snow)

Prepare a System disk (see [docs/SETUP.md](docs/SETUP.md)), then:

```bash
make run         # NSE + Snow (modem TCP bridge on 127.0.0.1:1984)
```

With **HelloMacintosh** frontmost on the emulated Plus, deploy the client in another terminal:

```bash
make deploy SNOW=1
# or: ./scripts/hellomacintosh --serial tcp://127.0.0.1:1984 run build/mac/RetroSlack.bin
```

### Real Plus over serial

Install and leave **HelloMacintosh** running on the Plus (see that repo’s README). Flash the Arduino passthrough there (`./scripts/flash-bridge.sh`). Then from RetroSlack:

```bash
make deploy SERIAL=/dev/cu.usbserial-XXXX
./build/host/nse --serial /dev/cu.usbserial-XXXX --baud 19200 --verbose
```

Stop NSE before the next `hellomacintosh stop` / `run` — only one host process can own the USB serial device.

`./scripts/hellomacintosh` is a wrapper around the cloned HelloMacintosh CLI (`HM_SERIAL` or `--serial`).

## Layout

- `protocol/` — portable RHTTP framing (host + Mac)
- `host/nse/` — desktop Network Serial Emulator (Slack + `http://rs/` over serial)
- `arduino/nse/` — experimental on-device Ethernet sketch (not the supported link)
- `mac/` — Retro68 guests (RetroSlack, RHTTPTest)
- `scripts/` — bootstrap, build, run, **serial deploy**
- `docs/` — setup, protocol, hardware, debugging, display API

The USB–serial Arduino firmware is in **[HelloMacintosh](https://github.com/kApp-Oliver/HelloMacintosh)** (`arduino/bridge`). `HelloMacintosh/` is a local clone only (gitignored). Hello World drafts under `tutorials/` are local for now (also gitignored).

## Docs

- [docs/SETUP.md](docs/SETUP.md) — Snow, Retro68, ROM, System, HelloMacintosh
- [docs/HARNESS.md](docs/HARNESS.md) — deploying RetroSlack with HelloMacintosh
- [docs/PROTOCOL.md](docs/PROTOCOL.md) — RHTTP wire format
- [docs/DISPLAY_API.md](docs/DISPLAY_API.md) — host Slack cache + tiny `http://rs/` payloads
- [docs/REAL_HARDWARE.md](docs/REAL_HARDWARE.md) — real Plus + Arduino
- [docs/DEBUGGING.md](docs/DEBUGGING.md) — Cursor / LLDB / serial logs
- [SECURITY.md](SECURITY.md) — tokens, ROMs, what not to commit

## Legal

Apple ROM and System software are **not** redistributed. You must supply your own legally obtained copies. Macintosh is a trademark of Apple Inc. Slack tokens stay on the macOS NSE — never on the Mac or in git.
