# Network Serial Driver (Mac)

The RHTTP client currently lives in [`../common`](../common) and is **linked into** RetroSlack / RHTTPTest for faster iteration.

Endgame packaging (same protocol, stock Serial Manager):

- Build an INIT/DRVR that exposes `RHTTPOpen` / `RHTTPRequest` to multiple apps
- Keep wire format identical to `docs/PROTOCOL.md`

No emulator-specific APIs — safe for a real Macintosh Plus modem port.
