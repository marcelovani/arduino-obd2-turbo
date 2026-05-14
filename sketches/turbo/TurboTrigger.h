// TurboTrigger.h — Detects the throttle-drop moment and fires the Turbo sound.
//
// Trigger conditions (all must be true simultaneously):
//   - prevTPS was above cfgThrottleHigh (hard acceleration)
//   - current TPS is below cfgThrottleLow (lifted off)
//   - RPM is above cfgRpmMin (still spinning hard)
//   - gear is within cfgMinGear..cfgMaxGear
//   - cooldown since last trigger has expired
//
// Real device: plays the spray MP3 via DFPlayer at gear-appropriate volume.
// Simulation:  fires tone() on PIN_BUZZER and sets turboSoundUntilMs for LED blink.
//
// Rate-limited in SIMULATION to 100 ms intervals to match the real OBD2 poll
// cadence — without this prevTPS updates every frame and the trigger never fires.

void checkTurbo(uint32_t now) {
#ifdef SIMULATION
  static uint32_t lastCheckMs = 0;
  if (now - lastCheckMs < 100) return;
  lastCheckMs = now;
#endif
  int gear = estimateGear(metricRPM, metricSpeed);
  if (prevTPS       > cfgThrottleHigh &&
      metricTPS     < cfgThrottleLow  &&
      metricRPM     > cfgRpmMin       &&
      gear         >= (int)cfgMinGear &&
      gear         <= (int)cfgMaxGear &&
      now - lastTurboMs > (uint32_t)cfgCooldownMs) {
    turboCount++;
    lastTurboMs  = now;
    turboUntilMs = now + 2000;
#ifdef SIMULATION
    turboSoundUntilMs = now + 1000;
    tone(PIN_BUZZER, 900, 350);
#else
    dfplayer.volume(gear == 1 ? (int)cfgVolGear1 : (int)cfgVolGear2);
    dfplayer.playMp3Folder(gear == 1 ? TRACK_SPRAY_GEAR1 : TRACK_SPRAY_GEAR2);
#endif
  }
  prevTPS = metricTPS;
}
