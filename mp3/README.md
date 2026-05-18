# mp3

Audio files played by the DFPlayer Mini. Copy the entire `mp3/` folder to the **root**
of the microSD card as-is — the sketch uses `playMp3Folder(n)` which looks up files by
name, so copy order does not matter.

## Files

| Filename   | Content              | When played                                     |
| ---------- | -------------------- | ----------------------------------------------- |
| `0001.mp3` | "Pairing"            | Each time the device starts scanning for ELM327 |
| `0004.mp3` | "OBD2 not connected" | Scan timeout (30 s) or Bluetooth connect fail   |
| `0008.mp3` | "Demo mode"          | When demo mode is turned on via the menu        |
| `0009.mp3` | "Goodbye"            | When the Power option is used to turn off       |
| `0010.mp3` | Long spray (alt)     | Alternative for 1st gear — not used by default  |
| `0011.mp3` | Faster spray (alt)   | Alternative for 2nd gear — not used by default  |
| `0012.mp3` | Default spray        | All gear triggers (1st, 2nd, 3rd)               |

Format the card as **FAT32**. The `mp3/` folder must be at the root of the card.

Voice clips were generated with [MiniMax Text-to-Speech](https://www.minimax.io/audio/text-to-speech).

## Required MP3 format

DFPlayer Mini (and its clones) are picky about file format. Files outside this
spec will play distorted, play silently, or not play at all:

| Property     | Required value          | Notes                                          |
| ------------ | ----------------------- | ---------------------------------------------- |
| MPEG version | MPEG-1 Layer III        | MPEG-2 / MPEG-2.5 cause distortion on clones   |
| Sample rate  | 44100 Hz                | Other rates work but 44.1 kHz is most reliable |
| Bit rate     | 128 kbps CBR            | VBR causes early cutoff or stuttering          |
| Channels     | Mono                    | Stereo works but wastes space for one speaker  |
| ID3 tags     | None (strip completely) | ID3v2 headers at file start cause glitches     |

Re-encode any non-conforming file with ffmpeg:

```bash
ffmpeg -i input.mp3 \
  -ar 44100 -ac 1 -b:a 128k \
  -f mp3 -id3v2_version 0 -write_id3v1 0 \
  output.mp3
```

After replacing a spray file (`0010.mp3` or `0011.mp3`), measure its duration and
update `SPRAY_DURATION_S` in [`viewer/server.py`](../viewer/server.py) so the
viewer's yellow annotation bands stay accurate:

```bash
ffprobe -v error -show_entries format=duration \
  -of default=noprint_wrappers=1:nokey=1 0010.mp3
```

## Speaker wiring

Connect the speaker **between SPK1 and SPK2** — both pins are live outputs from
the built-in BTL amplifier. Do not connect either pin to GND; grounding either
output shorts the amplifier and can damage the module.

```
  SPK1 ──── wire 1 ──┐
                     🔊
  SPK2 ──── wire 2 ──┘
```

## Idle hiss

The DFPlayer Mini's onboard amp stays active between tracks and amplifies
power-rail noise as an audible hiss. Fixes in order of effectiveness:

1. **100 µF capacitor across VCC/GND** — the ESP32's 3.3 V pin is noisy; a
   decoupling cap on the DFPlayer's power pins eliminates most of the hiss.
2. **`dfplayer.stop()` after playback** — on some clones this puts the amp in a
   lower-noise idle state; costs nothing in hardware.
3. **10–22 Ω resistors in series with each speaker wire** — passive low-pass
   filter that trims high-frequency hiss at a small volume cost.
