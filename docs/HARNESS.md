# HelloMacintosh + RetroSlack guests

The resident Plus loader, Arduino USB–serial bridge, and host CLI are **not** in this repository. They live in:

**[https://github.com/kApp-Oliver/HelloMacintosh](https://github.com/kApp-Oliver/HelloMacintosh)**

`make bootstrap` clones that repo (into `HelloMacintosh/` here, or `$HELLOMACINTOSH_DIR`). `./scripts/hellomacintosh` wraps its CLI. Full command docs: [HelloMacintosh HARNESS.md](https://github.com/kApp-Oliver/HelloMacintosh/blob/main/docs/HARNESS.md).

```
macOS  hellomacintosh CLI
   USB serial @ 19200 8N1
     Arduino Nano/Uno bridge (byte copy)
       RS-422
         Macintosh Plus  HelloMacintosh
           writes :Harness:  then _Launch
```

Snow: `--serial tcp://127.0.0.1:1984`. Same RetroSlack `.bin`.

## One-time Plus setup

Do this from a **HelloMacintosh** checkout (after `make bootstrap`, that is `$HELLOMACINTOSH_DIR` or `./HelloMacintosh`):

1. `make mac` in HelloMacintosh, copy the loader onto the Plus (floppy, SCSI, or that repo’s `make run` for Snow).
2. Leave **HelloMacintosh** running. It listens on the modem port at **19200 8N1**.

This RetroSlack tree only builds guests (`RetroSlack.bin`, `RHTTPTest.bin`, …).

## Everyday RetroSlack rebuild

Stop NSE first (one process owns the USB serial).

```bash
make mac
./scripts/hellomacintosh ping
./scripts/hellomacintosh run build/mac/RetroSlack.bin
# guest is up — now start Slack on the same serial:
./build/host/nse --serial /dev/cu.usbserial-XXXX --baud 19200 --verbose
```

Or `make deploy SERIAL=/dev/cu.usbserial-XXXX` then start NSE as printed.

Snow:

```bash
make deploy SNOW=1
./build/host/nse --serial tcp://127.0.0.1:1984 --verbose
```

`HM_SERIAL` or `--serial` selects the device. Close box, Cmd-Q, or `hellomacintosh stop` returns to HelloMacintosh (quit NSE first).

| Command | Action |
|---------|--------|
| `hellomacintosh ping` | PING/PONG against whoever owns the modem (loader or guest) |
| `hellomacintosh run FILE.bin` | Upload + launch (normal rebuild) |
| `hellomacintosh stop` | Quit the running guest |
| `hellomacintosh ls` / `status` / `rm` | Harness folder on the Plus |

Loader self-update is `hellomacintosh update` with **HelloMacintosh.bin** from the HelloMacintosh repo — never `run` the loader as a guest.

## Why guests need `add_harness_guest`

When the loader starts a guest it **hands off the modem**. Every target built with `add_harness_guest` links `mac/common/harness_guest.c`, which patches `WaitNextEvent` / `GetNextEvent` so ping/stop keep working. `put` cannot splice that runtime into a random third-party Mac app.

If the app also uses `RHTTPOpen` (RetroSlack), call `HarnessGuestSetPoll(RHTTPPollStop)` after a successful open (skip polling while a request is in flight).

```cmake
add_harness_guest(MyApp
    my_app/main.c
    TYPE "APPL"
    CREATOR "MyAp"
)
```

On quit, dispose windows then `HarnessReturnToLoader()`. The guest runtime looks for an application named **HelloMacintosh** on the volume.

## Wire format

Same [RHTTP frames](PROTOCOL.md). HelloMacintosh is an RHTTP client using `rh://` URLs (`PUT` / `PATCH` / `UPDATE` / `START` / `STOP` / …). Details stay in the HelloMacintosh docs so this repo does not fork the loader protocol.
