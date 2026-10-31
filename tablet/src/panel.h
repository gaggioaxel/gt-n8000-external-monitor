#ifndef NOTEDISPLAY_PANEL_H
#define NOTEDISPLAY_PANEL_H

struct panel_stats {
	float fps, mbit, decode_ms, fimc_ms;
	int stream_w, stream_h;
};

/* Impostazioni scelte sul tablet e applicate dal PC */
struct panel_settings {
	int portrait, fps, keepalive_ms;
};

/* Avvia il thread di input, che controlla anche il cavo: se si stacca avvisa e spegne il tablet */
int panel_start(void);
int panel_rotation(void);
/* Copia le impostazioni attuali e restituisce un contatore che cresce a ogni modifica */
unsigned int panel_settings(struct panel_settings *s);
void panel_set_stats(const struct panel_stats *s);
void panel_set_host(const char *encoder, float fps, float encode_ms);
/* Se il pannello è aperto lo copia nella pagina nascosta, prima del flip */
void panel_blit(void);
/* Senza stream: mostra il pannello nella pagina visibile, o lo cancella quando si chiude */
void panel_idle(void);

#endif
