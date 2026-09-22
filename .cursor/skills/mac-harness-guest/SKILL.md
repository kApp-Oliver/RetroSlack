---
name: mac-harness-guest
description: >-
  Registers RetroSlack-tree Macintosh apps as loader guests so
  hellomacintosh ping/stop/run work. Use when adding a Mac app in this
  repo, editing mac/CMakeLists.txt, or wiring HarnessInstallQuitAE,
  HarnessProcessAppleEvent, HarnessReturnToLoader, or WaitNextEvent.
---

# Harness guest apps (RetroSlack tree)

Anything you deploy with `./scripts/hellomacintosh run` must be a
**harness guest**: linked with `add_harness_guest` and sitting in a
`WaitNextEvent` / `GetNextEvent` loop.

Deploy with [rh-deploy](../rh-deploy/SKILL.md). This skill is the C/CMake side **here**.

The loader itself is [HelloMacintosh](https://github.com/kApp-Oliver/HelloMacintosh)
(`make bootstrap` clones it).

## Register in `mac/CMakeLists.txt`

```cmake
add_harness_guest(MyApp
    my_app/main.c
    TYPE "APPL"
    CREATOR "MyAp"
)
```

- First argument = CMake target **and** `build/mac/MyApp.bin` name.
- Creator is four MacRoman characters unused in this file (`RSlk`, `RhtT`, …).
  `HMac` is the HelloMacintosh loader (other repo).
- Do **not** list `common/harness.c`, `harness_guest.c`, `ser_modem.c`, or
  `size_multifinder.r` yourself.
- Do **not** use `add_application` for guests. That is for `RHTTPTest`.

Copy the event-loop shape of `mac/retroslack/main.c` (`WaitNextEvent`,
`HarnessInstallQuitAE`, `HarnessReturnToLoader`). The tiny Hello World guest
lives in [HelloMacintosh](https://github.com/kApp-Oliver/HelloMacintosh)
(`mac/hello_mac/main.c`).

```bash
make mac
./scripts/hellomacintosh run build/mac/MyApp.bin
```
