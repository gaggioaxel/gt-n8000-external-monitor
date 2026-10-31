# Risoluzione dei problemi

## Il monitor non compare sul PC

Controlla che il tablet sia visto e che il servizio sia partito:

```bash
lsusb | grep 18d1:2d00
```

```bash
systemctl --user status galaxynotedisplay.service
```

```bash
journalctl --user -u galaxynotedisplay.service -n 20
```

Se `lsusb` non lo mostra, il tablet non è in Display esterno. Se lo mostra ma il servizio non parte, stacca e ricollega il cavo: la regola udev scatta solo quando il dispositivo compare.

## Il mouse scatta o l'immagine trascina

- **Scatti:** Mutter consegna meno frame del dovuto se il suo limite coincide con i 60 Hz. L'host gli chiede 120 fps e limita a 60 per conto suo: controlla di usare l'host di questa repo.
- **Trascinamento:** i frame si stanno accumulando. La voce `coda` in `/tmp/nd.log` sul tablet deve restare a 1. Abbassa Limite fps dal pannello.

## Immagine capovolta

Scegli l'orientamento dal pannello. Controlla anche che in GNOME l'orientamento sia Landscape.

## Il tablet si riavvia da solo

Se `notedisplayd` muore, il tablet riparte. Da Android, il log del crash è in `/proc/last_kmsg`:

```bash
adb shell "su -c 'cat /proc/last_kmsg'" > last_kmsg.txt
```

## Vedere cosa succede sul tablet

In Display esterno il tablet ha una rete USB: il PC riceve `172.16.42.2`, il tablet è `172.16.42.1` con una shell telnet senza password. Il servizio è raggiungibile solo da chi è collegato al cavo.

```bash
telnet 172.16.42.1
```

Il log è in `/tmp/nd.log`.

## Il tablet non si avvia più

1. Metti il tablet in Download mode: spento, tieni premuti Accensione e Volume giù, poi conferma con Volume su.
2. Scrivi il BOOT originale dal backup:

```bash
heimdall flash --BOOT tablet/backup/BOOT.img
```

Si può eseguire un solo comando `heimdall` per ogni sessione di Download mode.

Se il tablet resta acceso e bloccato, tieni premuto Accensione finché non si riavvia.
