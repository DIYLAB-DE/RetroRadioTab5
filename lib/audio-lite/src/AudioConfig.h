#ifndef AUDIO_LITE_AUDIOCONFIG_H
#define AUDIO_LITE_AUDIOCONFIG_H

/**
 * @file AudioConfig.h
 * @brief Funktionsschalter von audio-lite
 *
 * Diese Schalter legen fest, welche Decoder einkompiliert werden. Jeder Wert kann über ein Build-Flag vorgegeben werden und behält sonst die hier stehende Vorgabe.
 *
 * Gegenüber der Referenz-Library fehlen die Schalter `AUDIO_FEATURE_VU` und `AUDIO_FEATURE_SPECTRUM`, und `AUDIO_CODEC_OPUS` ist auf eine feste 0 festgelegt. Der Opus-Decoder wird nicht mitgeliefert, und VU-Pegel wie Spektrum werden nicht berechnet: Der Host ruft `getVUlevel()` nie auf, und beide Feature-Schalter standen auf false. Ein Schalter, den man nicht einschalten kann, wäre eine Falle.
 */

// ===================================================================
// Decoder
// ===================================================================

#ifndef AUDIO_CODEC_WAV
    #define AUDIO_CODEC_WAV 1
#endif

#ifndef AUDIO_CODEC_MP3
    #define AUDIO_CODEC_MP3 1
#endif

#ifndef AUDIO_CODEC_AAC
    #define AUDIO_CODEC_AAC 1
#endif

#ifndef AUDIO_CODEC_M4A
    #define AUDIO_CODEC_M4A 1
#endif

#ifndef AUDIO_CODEC_FLAC
    #define AUDIO_CODEC_FLAC 1
#endif

#ifndef AUDIO_CODEC_VORBIS
    #define AUDIO_CODEC_VORBIS 1
#endif

// VU-Pegel und Spektrum werden nicht berechnet. Beide Schalter stehen fest auf 0, weil Audio.cpp sie in #if-Ausdrücken benutzt und "undefiniert" dort nur zufällig dasselbe bedeutet. Explizit ist es nachlesbar und überlebt ein -Wundef.
#define AUDIO_FEATURE_VU 0
#define AUDIO_FEATURE_SPECTRUM 0

// Der Opus-Decoder wird nicht mitgeliefert. Der Schalter bleibt als feste 0 bestehen, weil Audio.cpp sechs #if-Blöcke darauf stützt. Einschalten lässt er sich nicht.
#define AUDIO_CODEC_OPUS 0

// ===================================================================
// Diagnose
// ===================================================================

// Diagnose: Statuszeile alle 500 ms in Audio::loop() plus Hexdump bei Decoder-Fehlern. Im Normalbetrieb 0, bei Fehlersuche per Build-Flag oder hier auf 1 setzen. Die serielle Ausgabe kostet Rechenzeit im Audio-Umfeld.
#ifndef AUDIO_LITE_DIAG
    #define AUDIO_LITE_DIAG 0
#endif

// 1 = ICY-Metadaten NICHT anfordern (Vergleichsschalter für die Fehlersuche). Im Normalbetrieb 0, sonst fehlt die Titelanzeige (StreamTitle) beim Webradio.
#ifndef AUDIO_LITE_NO_ICY
    #define AUDIO_LITE_NO_ICY 0
#endif

// ===================================================================
// Klangregelung
// ===================================================================

// Pegelreserve der Klangregelung: keine, immer oder abhängig von der aktuellen Lautstärke.
#define AUDIO_EQ_HEADROOM_NONE 0
#define AUDIO_EQ_HEADROOM_ALWAYS 1
#define AUDIO_EQ_HEADROOM_VOLUME_AWARE 2

#ifndef AUDIO_EQ_HEADROOM_MODE
    #define AUDIO_EQ_HEADROOM_MODE AUDIO_EQ_HEADROOM_VOLUME_AWARE
#endif

// ===================================================================
// Konsistenzprüfungen
// ===================================================================

#if AUDIO_CODEC_M4A && !AUDIO_CODEC_AAC
    #error "AUDIO_CODEC_M4A benötigt AUDIO_CODEC_AAC"
#endif

#if AUDIO_EQ_HEADROOM_MODE < AUDIO_EQ_HEADROOM_NONE || AUDIO_EQ_HEADROOM_MODE > AUDIO_EQ_HEADROOM_VOLUME_AWARE
    #error "AUDIO_EQ_HEADROOM_MODE ist ungültig"
#endif

#endif // AUDIO_LITE_AUDIOCONFIG_H
