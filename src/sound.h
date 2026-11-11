/* sound.h - sound effects and music for the Amiga build (native/amiga_audio: Paula, AHI on MorphOS).
 * Effects: config/sfx.txt maps a game sound name to sfx/wave/<file>.wav (EUS1 in sfx.pak), cached in
 * Chip RAM. Music: raw 8-bit PCM 28 kHz tracks data/music/mNN.pcm (EUM1, no decoding) streamed on channels 2+3, playlists from
 * music/<list>.txt (frontend, 14..17 by century, early/late), names resolved via music/index.txt. */
#ifndef EU2_SOUND_H
#define EU2_SOUND_H
#include "pak.h"
int  sound_init(Pak *sfx, Pak *text);          /* 0 = ok (or silent: audio unavailable) */
void sound_free(void);
void sound_play(const char *name);             /* e.g. "if_click", "army_victory" */
void sound_music_list(const char *list);       /* switch playlist: "frontend", "14" .. "17" */
void sound_service(void);                      /* call every frame: refills music, next track */
#endif
