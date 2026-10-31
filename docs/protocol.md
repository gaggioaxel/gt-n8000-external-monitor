# USB protocol

The tablet presents itself as an Android accessory (`18d1:2d00`), with two bulk endpoints on interface 0. There is no AOA exchange.

Every packet has this form:

```text
[0xA5] [type u8] [length u32 big-endian] [payload]
```

The `0xA5` byte is used to resynchronize after a reconnection.

| Type | Direction | Payload |
| --- | --- | --- |
| `0x00` noop | PC → tablet | empty |
| `0x01` config | PC → tablet | width u16, height u16, SPS length u16, SPS, PPS (Annex-B) |
| `0x02` frame | PC → tablet | Annex-B H.264 access unit |
| `0x03` keyframe | PC → tablet | like `0x02`, IDR frame |
| `0x04` info | PC → tablet | text: `encoder fps encode_ms` |
| `0x13` settings | tablet → PC | portrait u8, fps u8, keepalive ms u16 |

A noop is added when a transfer would be an exact multiple of 512 bytes: without it, the tablet would wait for more data.

## Sequence

1. At the start of a session the tablet sends `0x13`, so the PC follows the choice made on the tablet.
2. The PC sends config and then a keyframe.
3. The tablet initializes the decoder with SPS, PPS and IDR in the same buffer.
4. Every 2 seconds the PC sends `0x04` for the panel.

If the config changes, for example when switching to portrait, the tablet closes and reopens the decoder.

The `f_accessory` driver does not support `FIONREAD`: read in 16 KB blocks, without `available()`.
