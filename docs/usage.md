# Usage

## Power-on

The menu stays for 5 seconds and starts from the most recent choice. If you never made one, External display starts.

| Action | Effect |
| --- | --- |
| Volume up/down | moves the selection and stops the countdown |
| Power | starts the selected entry |
| Touch on an entry | starts it right away |

The choice survives power-off. Recovery is not remembered: after a visit to TWRP the menu goes back to the previous choice.

The `Recovery (TWRP)` entry reboots into the RECOVERY partition. Only TWRP was tested, and the label does not change if you install another recovery.

If you choose Android it stays the default: the monitor will not appear by itself until you choose External display again.

## Cable

| Situation | What happens |
| --- | --- |
| You plug in the cable with the display running | the monitor appears on the PC |
| You unplug the cable | the monitor goes away, the tablet shows a warning and powers off after 10 seconds |
| You plug it back within 10 seconds | the warning goes away and the monitor returns |
| You power on without a cable | the display starts after the menu, then the warning and power off |

The screen stays black until the PC sends the first frame.

## Panel

A touch anywhere, Power or Volume opens it. A touch outside or Power closes it, and it closes by itself after 6 seconds.

| Entry | Meaning |
| --- | --- |
| Brightness | slider; the minimum is 4%, so the screen never goes black. Volume also changes it by 5% |
| Land, Port, Land 180, Port 180 | orientation. In portrait the PC recreates the monitor at 800×1280 |
| FPS limit | 30, 45 or 60: lowering it reduces the tablet's work |
| Repeat ms | how long after the last change the PC resends the image. It does not affect smoothness: leave it at 50 |
| Video, Tablet, PC | fps, bitrate, decode and conversion times, encoder and encode time |

These settings go back to their defaults at every boot.

## PC

The host starts by itself. To see its statistics, stop the service and start it by hand:

```bash
systemctl --user stop galaxynotedisplay.service
```

```bash
GalaxyNoteDisplay --stats
```

Useful options are `--encoder va` for Intel hardware encoding, `--crf` for quality and `--match-main` for a virtual monitor as large as the main one.
