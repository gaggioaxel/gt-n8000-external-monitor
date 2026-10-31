# Storia del progetto

Il progetto è nato per usare il tablet come schermo esterno a bassa latenza, soprattutto per il disegno. Ha seguito una strada diversa da quella pensata all'inizio.

| Fase | Esito |
| --- | --- |
| App Android con MediaCodec | funzionava, ma con SurfaceFlinger e Java di mezzo |
| Host Wayland con monitor virtuale | funzionante; lo schermo fittizio su Xorg non si accendeva |
| Linux mainline 7.2 sul tablet | abbandonato: pannello bianco e USB gadget assente |
| Kernel Samsung con initramfs proprio | funzionante al primo tentativo |
| Decodifica MFC e conversione FIMC | prima immagine senza Android |
| 60 fps veri | risolto il limite di Mutter |
| Menu di avvio e host automatico | l'uso quotidiano non richiede interventi |
| Tocco e penna verso il PC | scartati |

## Perché non il kernel mainline

Il device tree del p4note esiste, ma mancano MFC, FIMC e Wacom. Il decompressore si bloccava attivando la cache, e il kernel arrivava in fondo con il pannello bianco. Il kernel Samsung offre già `/dev/s3c-mfc`, la FIMC e `/dev/usb_accessory`, quindi lo stesso risultato è arrivato con molto meno lavoro.

## Tocco e penna

Il tocco è stato inoltrato al monitor virtuale con l'API RemoteDesktop di Mutter e funzionava tecnicamente, ma non era comodo da usare. La penna era già andata male su Android e non è stata portata qui. Il sistema è rimasto solo video, e il touch serve ad aprire il pannello.

## Non misurato

La latenza totale dal PC al pannello non è stata misurata con una fotocamera. I tempi in questa repo sono quelli interni al tablet.
