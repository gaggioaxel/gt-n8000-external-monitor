# Uso

## Accensione

Il menu resta 5 secondi e parte dalla scelta più recente. Se non ne hai mai fatta una, parte Display esterno.

| Azione | Effetto |
| --- | --- |
| Volume su/giù | sposta la selezione e ferma il conto alla rovescia |
| Accensione | avvia la voce selezionata |
| Tocco su una voce | la avvia subito |

La scelta resta anche dopo aver spento il tablet. Recovery non viene ricordata: dopo una visita a TWRP il menu torna alla scelta precedente.

Se scegli Android, resterà il predefinito: il monitor non comparirà da solo finché non scegli di nuovo Display esterno.

## Cavo

| Situazione | Cosa succede |
| --- | --- |
| Colleghi il cavo a display acceso | sul PC compare il monitor |
| Stacchi il cavo | il monitor sparisce, il tablet mostra un avviso e si spegne dopo 10 secondi |
| Ricolleghi entro 10 secondi | l'avviso sparisce e il monitor torna |
| Accendi senza cavo | dopo il menu parte il display, poi l'avviso e lo spegnimento |

Lo schermo resta nero finché il PC non invia il primo frame.

## Pannello

Si apre con un tocco ovunque, con Accensione o con Volume. Si chiude con un tocco fuori, con Accensione, oppure da solo dopo 6 secondi.

| Voce | Significato |
| --- | --- |
| Luminosità | slider; il minimo è il 4%, così lo schermo non diventa mai nero. Anche Volume la cambia del 5% |
| Oriz, Vert, Oriz 180, Vert 180 | orientamento. In verticale il PC ricrea il monitor a 800×1280 |
| Limite fps | 30, 45 o 60: abbassarlo riduce il lavoro del tablet |
| Ripeti ms | dopo quanto il PC rimanda l'ultima immagine a schermo fermo. Non influisce sulla fluidità: lascia 50 |
| Video, Tablet, PC | fps, bitrate, tempi di decodifica e conversione, encoder e tempo di codifica |

Queste impostazioni tornano ai valori iniziali a ogni avvio.

## PC

L'host parte da solo. Per vedere le statistiche, avvialo a mano dopo aver fermato il servizio:

```bash
systemctl --user stop galaxynotedisplay.service
```

```bash
GalaxyNoteDisplay --stats
```

Le opzioni utili sono `--encoder va` per la codifica hardware Intel, `--crf` per la qualità e `--match-main` per un monitor virtuale grande quanto quello principale.
