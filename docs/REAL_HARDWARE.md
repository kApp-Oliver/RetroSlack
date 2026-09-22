# Real Macintosh Plus + Arduino

Hardware path (same Mac binaries as Snow). Slack stays on **macOS**; the Arduino only copies bytes.

```
macOS  NSE / hellomacintosh
  USB serial @ 19200 8N1
    Arduino (passthrough; HelloMacintosh arduino/bridge)
      RS-422 conversion (TTL UART ↔ differential)
        Macintosh Plus modem port (Mini-DIN-8)
```

Do not put the Arduino on Ethernet for this path. The host opens `/dev/cu.usbserial-*` (or `usbmodem`); the Plus sees RS-422 on the modem Mini-DIN-8.

## Checklist

- [ ] Mac Plus with System 6.0.8 or 7.0.1
- [ ] **[HelloMacintosh](https://github.com/kApp-Oliver/HelloMacintosh)** installed and running on the Plus
- [ ] AppleTalk **off** if using the printer port for debug logs
- [ ] RS-422 ↔ TTL adapter between Arduino UART and the Plus (Mac serial is **not** 5 V TTL)
- [ ] Tx/Rx crossed correctly (Mac Tx → Arduino Rx, Mac Rx ← Arduino Tx) + common ground
- [ ] Arduino flashed with HelloMacintosh `arduino/bridge` (`./scripts/flash-bridge.sh` in that repo)
- [ ] Baud **19200 8N1** on Mac, Arduino, and host (see `protocol/rhttp.h`)
- [ ] `hellomacintosh ping` works before `run` / NSE

## USB-serial + loader

After `make bootstrap`:

```bash
# in the HelloMacintosh checkout:
./scripts/flash-bridge.sh
# from RetroSlack:
make mac
make deploy SERIAL=/dev/cu.usbserial-XXXX
./build/host/nse --serial /dev/cu.usbserial-XXXX --baud 19200 --verbose
```

Do not run desktop NSE and `hellomacintosh` on the same USB device at the same time.

Nano/Uno wiring (see HelloMacintosh `arduino/bridge`): do not use D0/D1 for the Mac — those pins are USB serial. Typical MAX490-style: TXD→D8, RXD→D9, VCC→5V, GND common. RS-422: Mac TxD+ → R+, TxD− → R−, RxD+ → T+, RxD− → T−.

## Mini-DIN-8 (modem port)

Classic Mac serial uses differential **RS-422** on an 8-pin Mini-DIN (modem/printer). Use a proper Mac serial breakout or transceiver (e.g. 26LS31/32 or a commercial Mac→TTL adapter). Do not wire DIN-8 data pins straight to Arduino GPIO.

Pinouts vary by cable; verify with a known-good Mac serial reference before connecting a real Plus.

## Firmware

The supported sketch is the **byte-copy bridge** in HelloMacintosh:

```bash
./scripts/flash-bridge.sh   # from the HelloMacintosh checkout
```

`arduino/nse/` in this repo is an experimental on-device Ethernet HTTP proxy. It is **not** how RetroSlack talks to a Plus today.
