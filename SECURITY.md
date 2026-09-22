# Security

## Do not commit

| Path | Why |
|------|-----|
| `.env` | Slack user/bot tokens |
| `arduino/nse/secrets.h` | Token + optional static IP |
| `ROMS/**`, `*.ROM` | Apple ROM dumps (copyrighted; also not yours to publish) |
| `assets/disks/**` except `.gitkeep` | System 6/7 images, StuffIt archives |
| `*.hda`, `*.dsk`, `*.img` | Disk images that may contain System software |
| Private keys (`*.pem`, `id_rsa`, …) | Unrelated credentials that must never land in git |

Templates that **are** safe to commit: `.env.example`, `arduino/nse/secrets.h.example` (placeholder `xoxb-your-token-here` only).

`make bootstrap` does not write secrets. Copy the examples locally.

## Where tokens live

RetroSlack on the Macintosh never sees a Slack token. The Mac talks `http://rs/` over serial. The desktop NSE (or Arduino firmware) injects `Authorization` when calling `slack.com`.

Verbose NSE logs print `token=yes` / `token=no`, not the secret.

## If a token leaked

Revoke it in the Slack app dashboard and issue a new one. Do not rely on rewriting git history after a public push — assume anything pushed is public forever.
