#!/usr/bin/env bash
# Hardware path reminder: Arduino is USB-serial passthrough; Slack is host NSE.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
echo "Hardware path:"
echo "  macOS (NSE / hellomacintosh)"
echo "    → USB serial"
echo "      → Arduino (byte copy)"
echo "        → RS-422"
echo "          → Macintosh Plus Mini-DIN-8 modem port"
echo
echo "See: $ROOT/docs/REAL_HARDWARE.md"
echo
echo "1) Flash the passthrough (HelloMacintosh checkout):  ./scripts/flash-bridge.sh"
echo "2) Wire Mini-DIN-8 via RS-422↔TTL to the Arduino UART @ 19200 (not USB D0/D1)"
echo "3) Leave HelloMacintosh running on the Plus"
echo "4) From RetroSlack:"
echo "     make bootstrap && make mac"
echo "     make deploy SERIAL=/dev/cu.usbserial-XXXX"
echo "     ./build/host/nse --serial /dev/cu.usbserial-XXXX --baud 19200 --verbose"
