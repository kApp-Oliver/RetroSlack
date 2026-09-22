# RHTTP — HTTP over serial

Binary framed protocol between the Macintosh (Serial Manager) and an NSE peer (desktop or Arduino). Default link: **19200 8N1**.

## Frame layout (little-endian)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | magic `RHTP` (`0x52 0x48 0x54 0x50`) |
| 4 | 1 | version (`1`) |
| 5 | 1 | type |
| 6 | 2 | flags |
| 8 | 4 | id |
| 12 | 4 | header_len |
| 16 | 4 | body_len |
| 20 | header_len | header bytes (UTF-8 text) |
| 20+header_len | body_len | body bytes |

Maximum guest body/header sizes are defined in `protocol/rhttp.h` (`RHTTP_MAX_HEADER`, `RHTTP_MAX_BODY`).

## Types

| Value | Name | Direction |
|-------|------|-----------|
| 1 | REQ | Mac → NSE |
| 2 | RES | NSE → Mac |
| 3 | ERR | NSE → Mac |
| 4 | PING | either |
| 5 | PONG | either |

### Flags

- `0x0001` — `RHTTP_FLAG_MORE` — more body chunks follow (same id)

## REQ header text

Lines separated by `\n`:

```
METHOD GET
URL https://slack.com/api/conversations.list
Header: Content-Type: application/json
```

Do **not** send Slack tokens from the Mac. RetroSlack uses the host display API
(`http://rs/...` — see [DISPLAY_API.md](DISPLAY_API.md)); the NSE talks to Slack
and injects `Authorization` there. Passthrough allowlisted HTTPS (e.g. RHTTPTest)
still works the same way.

## RES header text

```
STATUS 200
Header: Content-Type: application/json
```

## ERR header text

```
REASON curl failed: ...
```

Body may be empty or contain a short diagnostic.

## Handshake

After open, either side may send `PING`; peer replies `PONG` with the same `id`.

## HelloMacintosh (`rh://`)

The host **`hellomacintosh` CLI** ([HelloMacintosh](https://github.com/kApp-Oliver/HelloMacintosh) repo) is also an RHTTP client: it sends REQ/PING, the Plus loader replies RES/ERR/PONG. Frame layout, baud, and magic are unchanged. Command docs: [HelloMacintosh HARNESS.md](https://github.com/kApp-Oliver/HelloMacintosh/blob/main/docs/HARNESS.md). RetroSlack’s wrapper is `./scripts/hellomacintosh` after `make bootstrap`.
