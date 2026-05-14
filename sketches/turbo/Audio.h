// Audio.h — DFPlayer Mini wrapper.
//
// Guarded by #ifndef SIMULATION: Wokwi uses tone() on PIN_BUZZER instead,
// and the DFRobotDFPlayerMini library is not available in the sim build.
//
// dfplayerVoice() is for spoken announcements only. Spray sounds are handled
// directly in TurboTrigger.h so each trigger can set its own volume.

#ifndef SIMULATION
DFRobotDFPlayerMini dfplayer;

// Plays a voice track at cfgVolVoice then silences the amp to kill idle hiss.
static void dfplayerVoice(int track) {
  dfplayer.volume((int)cfgVolVoice);
  dfplayer.playMp3Folder(track);
  delay(VOICE_PLAY_MS);
  dfplayer.stop();
  dfplayer.volume(0);
}
#endif
