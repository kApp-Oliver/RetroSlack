/*
 * Continuity probe over USB serial.
 * D2 = probe (INPUT_PULLUP). Nano GND = black (or whichever colour you clip).
 * SHORT = D2 is connected to GND. OPEN = not connected.
 */

#include <Arduino.h>

static const uint8_t kProbePin = 2;
static const unsigned long kHeartbeatMs = 400;

void setup() {
  pinMode(kProbePin, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("=== RetroSlack DIN-8 continuity ==="));
  Serial.println(F("D2=probe  GND=clip to one colour"));
  Serial.println(F("SHORT = that colour hits this DIN pin"));
  Serial.println(F("OPEN  = no connection"));
}

void loop() {
  static int last = -1;
  static unsigned long lastPrint = 0;
  int now = digitalRead(kProbePin) == LOW ? 1 : 0;

  digitalWrite(LED_BUILTIN, now ? HIGH : LOW);

  unsigned long t = millis();
  if (now != last || (t - lastPrint) >= kHeartbeatMs) {
    last = now;
    lastPrint = t;
    if (now) {
      Serial.println(F("SHORT  D2 connected to GND"));
    } else {
      Serial.println(F("OPEN   no connection"));
    }
  }
}
