# Protocollo USB

Il tablet si presenta come accessorio Android (`18d1:2d00`), con due endpoint bulk sull'interfaccia 0. Non c'è scambio AOA.

Ogni pacchetto ha questa forma:

```text
[0xA5] [tipo u8] [lunghezza u32 big-endian] [payload]
```

Il byte `0xA5` serve a risincronizzarsi dopo una riconnessione.

| Tipo | Direzione | Payload |
| --- | --- | --- |
| `0x00` noop | PC → tablet | vuoto |
| `0x01` config | PC → tablet | larghezza u16, altezza u16, lunghezza SPS u16, SPS, PPS (Annex-B) |
| `0x02` frame | PC → tablet | access unit H.264 Annex-B |
| `0x03` keyframe | PC → tablet | come `0x02`, frame IDR |
| `0x04` info | PC → tablet | testo: `encoder fps ms_codifica` |
| `0x13` impostazioni | tablet → PC | verticale u8, fps u8, keepalive ms u16 |

Il noop si aggiunge quando un trasferimento sarebbe un multiplo esatto di 512 byte: senza, il tablet aspetterebbe altri dati.

## Sequenza

1. All'avvio di una sessione il tablet invia `0x13`, così il PC si adegua alla scelta fatta sul tablet.
2. Il PC invia config e poi un keyframe.
3. Il tablet inizializza il decoder con SPS, PPS e IDR nello stesso buffer.
4. Ogni 2 secondi il PC invia `0x04` per il pannello.

Se la config cambia, per esempio passando in verticale, il tablet chiude e riapre il decoder.

Il driver `f_accessory` non supporta `FIONREAD`: si legge a blocchi di 16 KB, senza `available()`.
