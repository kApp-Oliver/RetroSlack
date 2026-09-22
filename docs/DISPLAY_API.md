# RetroSlack display API (`http://rs/`)

RetroSlack on the Mac does **not** call the Slack Web API directly. The desktop NSE
owns Slack HTTPS, caches the open channel/thread, and serves tiny display payloads
over the same RHTTP framing used for ordinary HTTP.

Tokens stay on the host (`.env`). The Mac only ever talks to `http://rs/...`.

## Why

Full `conversations.history` JSON over 19200 baud is slow on a Plus. The Mac only
needs the rows it will paint (~14 messages). History navigation asks for **one**
new edge message at a time.

## Endpoints

| Method | Path | Role |
|--------|------|------|
| GET | `/auth` | `auth.test` → `{ok,uid,user}` |
| GET | `/channels` | compact channel list |
| POST | `/open` | body `{"channel":"C…"}` or `{"channel":"C…","thread":"…"}` — focus + refresh cache + first viewport |
| GET | `/view?off=N&n=14` | viewport from cache (refresh if stale) |
| GET | `/nav?dir=older\|newer&n=14` | shift viewport by one; body is a **single** `msg` plus meta |
| GET | `/msg?i=N` | one cached message by index |
| POST | `/post` | body `{"text":"…"}` — post to open channel/thread; returns viewport |
| POST | `/react` | body `{"ts":"…","name":"clap","add":true\|false}` — returns viewport |

Passthrough allowlisted HTTPS (e.g. `example.com` for RHTTPTest) still works.
Direct `https://slack.com/api/...` from the Mac is no longer used by RetroSlack.

## Compact message shape

```json
{"u":"alice","uid":"U…","t":"hello","ts":"1712….","tt":"","n":1712,"rc":0,"r":[{"n":"clap","c":2,"m":1}]}
```

Display names are resolved on the host (`users.info` + cache).

## Cache

- Up to 100 messages for the open channel or thread.
- Refreshed from Slack when serving `/view` / `/nav` if older than ~8 seconds, and always on `/open`, `/post`, `/react`.

## Arduino

On hardware, the Arduino is a USB–serial ↔ RS-422 passthrough. Slack still runs on the
macOS NSE (`http://rs/` over that USB device). An on-device Ethernet NSE in
`arduino/nse/` is experimental and is not the supported path.
