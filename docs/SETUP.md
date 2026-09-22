# Emulator setup (Snow + Retro68)

## HelloMacintosh

Serial deploy (streaming `.bin` files to the Plus or to Snow) needs the **[HelloMacintosh](https://github.com/kApp-Oliver/HelloMacintosh)** repo. `make bootstrap` clones it; or:

```bash
git clone https://github.com/kApp-Oliver/HelloMacintosh.git HelloMacintosh
# or a sibling checkout:
export HELLOMACINTOSH_DIR=/path/to/HelloMacintosh
```

Build the loader and CLI **in that checkout**. If Retro68 was installed by RetroSlack’s bootstrap, point HelloMacintosh at it:

```bash
export RETRO68="$PWD/deps/Retro68-build/toolchain"
cd HelloMacintosh   # or $HELLOMACINTOSH_DIR
make host
make mac
```

RetroSlack’s `./scripts/hellomacintosh` wraps the CLI. Loader install on a System 6 HD is documented in HelloMacintosh’s [SETUP.md](https://github.com/kApp-Oliver/HelloMacintosh/blob/main/docs/SETUP.md).

## ROM

Place a Macintosh Plus ROM at:

```
ROMS/Mac-Plus.ROM
```

This path is gitignored. Never commit ROMs. Snow only accepts known SHA-256 digests (Plus v1/v2/v3). A raw Plus ROM is **131072** bytes (128 KiB). Some dumps (e.g. 138576) include a short wrapper — `scripts/run.sh` writes `ROMS/Mac-Plus-128k.ROM` from the **first** 128 KiB and launches that. Do not point Snow at the wrapped full dump.

Verify:

```bash
./scripts/bootstrap.sh --check-rom
```

A real Macintosh Plus does not need a ROM file in this tree.

## System software

You need a bootable System **6.0.8** or **7.0.1** disk/HD image for the Plus (≤4 MB RAM). Place images under `assets/disks/` (gitignored).

If you drop a StuffIt archive such as `assets/disks/MacOS701.sit`, `scripts/run.sh` will extract it (needs `brew install unar`). That archive contains install floppies (`Disk Tools.img`, `Install 1.img`, …).

Suggested first-boot flow in Snow:

1. `./scripts/make-boot-hd.sh` once (or let `run.sh` do it) — converts **Disk Tools** into a Snow SCSI device image (`System701-Boot.hda`). A Mac Plus cannot boot the stock 1.44 MB Disk Tools floppy (800 K drives only).
2. `make run` / `./scripts/run.sh` loads `assets/snow/RetroSlack.snoww` (Plus ROM + boot HD + RetroSlack floppy) and starts the NSE.
3. You should reach the Disk Tools desktop (not a flashing `?` disk). From there you can run Apple HD SC Setup on a larger blank HD (`System701-Plus.hda`) if you want a fuller install — note Install 2/Fonts/etc. in this Garden set look truncated; a complete 7.0.1 set or System 6.0.8 800 K disks may be needed for a full install.

For serial-streamed guests you typically want **System 6.0.8** plus HelloMacintosh on the HD (HelloMacintosh’s `make run` installs the loader). RetroSlack’s `make run` starts NSE + Snow; deploy the guest with `make deploy SNOW=1` once HelloMacintosh is frontmost.

## Bootstrap

```bash
./scripts/bootstrap.sh
# or: make bootstrap
```

Installs (via Homebrew when available): cmake, ninja, curl, hfsutils, clones/builds Retro68, downloads Snow into `deps/` (gitignored), clones HelloMacintosh, and builds the desktop NSE plus `hellomacintosh` CLI.

ROM is optional at bootstrap time (required for `make run`).

## Desktop NSE + Snow

```bash
make host
./scripts/run.sh
```

`run.sh` starts the NSE against Snow’s modem TCP bridge (`127.0.0.1:1984` by default) and launches Snow with the normalized `ROMS/Mac-Plus-128k.ROM` (Plus v3 hash).

Enable the modem port **TCP bridge** in Snow (Ports → Modem → Enable TCP bridge). NSE connects as a client to that port.

Do not run NSE and `hellomacintosh` on that TCP port at the same time. Deploy the guest first, then start NSE (or use `make deploy SNOW=1` after Snow is up with HelloMacintosh running).

## Slack identity

Tokens stay in `.env` on the host (or Arduino `secrets.h`) — never on the Mac. See [SECURITY.md](../SECURITY.md).

| Goal | Env var | Token |
|------|---------|--------|
| Post/list as the **app bot** | `SLACK_BOT_TOKEN` | `xoxb-…` |
| Act as **you** | `SLACK_USER_TOKEN` | `xoxp-…` |

NSE uses the first non-empty of `SLACK_USER_TOKEN`, `SLACK_TOKEN`, `SLACK_BOT_TOKEN`.

RetroSlack does **not** call `slack.com` from the Mac. The NSE exposes a display API at `http://rs/` (auth, channels, open/view/nav, post, react) and keeps a refreshed cache for the open channel/thread — see [DISPLAY_API.md](DISPLAY_API.md).

For a user token: in your Slack app → **OAuth & Permissions** → add **User Token Scopes** (`channels:read`, `groups:read`, `channels:history`, `groups:history`, `chat:write`, `users:read`, `reactions:write`), reinstall the app to your workspace, then copy the **User OAuth Token**. Restart the NSE after updating `.env`.

## Retro68 Mac builds

```bash
make mac
make disk
```

Requires Retro68 under `deps/Retro68-build/toolchain` (created by bootstrap). First toolchain build can take **30–90 minutes** (cross-compiling GCC for m68k); later rebuilds of RetroSlack itself are seconds.

Products land in `build/mac/` (`RetroSlack.dsk`, `RHTTPTest.dsk`) and are copied to gitignored `assets/disks/`. Serial deploy uses `build/mac/RetroSlack.bin` via HelloMacintosh — you do not need a floppy for the everyday loop.
