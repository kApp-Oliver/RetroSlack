# Contributing

1. Never commit `ROMS/`, `*.ROM`, System disk images, `.env`, `arduino/nse/secrets.h`, or `tutorials/`. See [SECURITY.md](SECURITY.md).
2. Guest Mac code must use stock Serial Manager only (no emulator-only APIs).
3. Keep RHTTP framing identical across desktop NSE, Arduino, and Mac (`protocol/`, `docs/PROTOCOL.md`).
4. Serial deploy (`hellomacintosh`, Plus loader, Arduino passthrough) lives in **[HelloMacintosh](https://github.com/kApp-Oliver/HelloMacintosh)** — clone it with `make bootstrap` or set `HELLOMACINTOSH_DIR`. Do not vendor that tree into this repo.
5. Prefer small PRs: protocol/NSE, Mac UI, Arduino, docs.
