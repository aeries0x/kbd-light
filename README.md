# kbd-light

`aurora-kbd-light` drives an Apple silicon MacBook's keyboard backlight from its ambient light sensor, the way macOS does.

- **Follows the room:** the keyboard is at its brightest in the dark and fades out as the room gets brighter. Above `ON_LUX` it stays off; once lit, it stays on until `OFF_LUX`. Changes fade over about half a second.
- **Respects manual changes:** a change made by anyone else wins. Turning the backlight off (for example from the desktop's slider, or the desktop's idle dimming) keeps it off until it is turned on again. Any other level holds until the room's light changes about fourfold. When the desktop restores the last automatic level after idle dimming, automatic control resumes.
- **Lid and display sleep:** with the lid closed, or once every connected display has been powered off for a second (as when the desktop blanks the screens), the backlight is off. It fades back in when a display wakes, to the held level if one was set. The light sensor sits in the lid, so it is not read while the lid is closed or the displays sleep, nor, after the lid opens or a display wakes, until its reading stops rising: the level comes back as it was before.
- **Idle keyboard (optional):** with `AURORA_KBD_LIGHT_IDLE_SECONDS` set, the backlight fades down to `AURORA_KBD_LIGHT_IDLE_PERCENT` (off by default) after that many seconds without a key press on any keyboard, and back up at the next key press, after a resume from suspend, or when the lid opens or a display wakes. Only the timing of key events is read. It is off by default, since some desktops dim the keyboard backlight themselves when the session goes idle; it suits desktops that don't, such as Hyprland.

It reads the light sensor and writes the LED through sysfs, and reads the lid switch (and, for the idle setting, the keyboards) through evdev, so it needs nothing beyond libc. It works with any desktop and runs as a system service. It finds the first LED named `*kbd_backlight` and the first IIO device with `in_illuminance_input` (`aop-sensors-als` on Apple silicon). Without either it exits quietly.

## Settings

Set these in `/etc/default/aurora-kbd-light`:

| Variable | Default | Meaning |
| --- | --- | --- |
| `AURORA_KBD_LIGHT_FULL_LUX` | 2 | At or below this, the brightest automatic level |
| `AURORA_KBD_LIGHT_ON_LUX` | 60 | Below this the backlight comes on |
| `AURORA_KBD_LIGHT_OFF_LUX` | 120 | Above this it goes off again |
| `AURORA_KBD_LIGHT_MIN_PERCENT` | 10 | Level just after it comes on |
| `AURORA_KBD_LIGHT_MAX_PERCENT` | 50 | Level in the dark |
| `AURORA_KBD_LIGHT_IDLE_SECONDS` | 0 | Dim after this many seconds without a key press (0: never) |
| `AURORA_KBD_LIGHT_IDLE_PERCENT` | 0 | Level while dimmed, if lower than the current one (0: off) |

`aurora-kbd-light -v` logs each level it sets and each change it respects.

## Build

```sh
make
make install DESTDIR=... PREFIX=/usr
```

The service is `aurora-kbd-light.service`.
