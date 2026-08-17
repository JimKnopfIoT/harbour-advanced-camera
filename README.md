# harbour-advanced-camera-ext

Extended build of Adam Pigg's [Advanced Camera](https://github.com/piggz/harbour-advanced-camera)
for Sailfish OS. Installs next to the original (own package, icon
"Advanced Camera Ext", own settings) and adds, in the general settings
panel (the "..." button):

- **Microphone gain** (50–600 %): raises the PulseAudio record-stream
  volume of the running recording the moment it starts, live while it
  runs, and remembers it for the next ones. Fixes the far too quiet
  videos of ports whose Android audio HAL uses a tiny camcorder input
  gain (Xperia 10 III). Same mechanism as
  [harbour-micgain](https://github.com/JimKnopfIoT/harbour-micgain), but
  built in, so no separate tool and no post-processing is needed.
- **Video bitrate / compression** now goes down to 1 Mbit/s (upstream:
  6.4) and shows the resulting MB per minute, so videos can be stored
  small right away instead of being re-encoded afterwards. The hardware
  H.264 encoder honours the target bitrate; combine with a lower
  resolution (resolution button) for the smallest files.

Build: `mb2 -t SailfishOS-5.0.0.62-aarch64 build` (Sailfish Platform SDK).

---

# harbour-advanced-camera

This is a camera application for Sailfish which exposes all available camera paramters to the user.

The application is licensed under the GPLv2+, with some source files specifically licensed under the LGPLv2(.1)+ so they can be re-used.
