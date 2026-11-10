#ifndef STATIONS_DEFAULT_H
#define STATIONS_DEFAULT_H

#include <cstdint>

/**
 * @brief Kompakte Struktur für Default-Senderdaten (nur im Flash/PROGMEM)
 *
 * Enthält nur die persistenten Felder ohne Laufzeitdaten (x, y, lastPos). Wird beim ersten Start oder nach Factory-Reset als Grundlage verwendet.
 */
struct StationDefault
{
    const char *shortName;   ///< Kurzname für Skala/Presets
    const char *name;        ///< Vollständiger Sendername
    const char *url;         ///< Stream-URL
    bool preset;             ///< Favorit-Flag
};

/**
 * @brief Standard-Senderliste (70 Sender, liegt im Flash/PROGMEM)
 *
 * Diese Liste wird verwendet wenn:
 * - Die Stations-Partition leer oder korrupt ist (erster Start)
 * - Ein Factory-Reset der Senderliste durchgeführt wird
 *
 * Änderungen an dieser Liste erfordern ein Firmware-Update. Benutzer-Änderungen über die WebUI werden in der Flash-Partition gespeichert.
 */
static const StationDefault DEFAULT_STATIONS[] = {
    // rbb / Berlin-Brandenburg
    {"ANTENNE BB", "Antenne Brandenburg", "http://www.antennebrandenburg.de/livemp3", false},
    {"FRITZ", "Fritz", "http://www.fritz.de/livemp3", false},
    {"RBB24 INFOR.", "rbb24 Inforadio", "http://www.inforadio.de/livemp3", false},
    {"RADIO3", "radio3", "http://www.rbbkultur.de/livemp3", false},
    {"RBB 88.8", "rbb 88.8", "http://www.rbb888.de/livemp3", false},
    {"RADIOEINS", "radioeins", "http://radioeins.de/livemp3", true},
    {"BB RADIO", "BB RADIO", "http://streambbr.ir-media-tec.com/live/mp3-192/web/play.m3u", false},
    {"BRF 91.4", "Berliner Rundfunk", "http://stream.berliner-rundfunk.de/brf/mp3-192", true},
    {"105'5 SPREE-R.", "105'5 Spreeradio", "http://stream.spreeradio.de/spree/mp3-192", false},
    {"ENERGY BLN.", "Energy Berlin", "http://nrj.de/digital", false},

    // Berliner Privatsender
    {"PURE FM", "pure fm - berlins dance radio", "http://s4.radionetz.de/purefm-bln.mp3", true},
    {"JAZZRADIO", "JazzRadio 106.8", "http://streaming.radio.co/s774887f7b/listen", false},
    {"METROPOL", "Metropol", "http://streams.metropolfm.de/MetropolFM/stream.aac", false},
    {"KLASSIK R.", "Klassik Radio", "http://live.streams.klassikradio.de/klassikradio-deutschland/stream/mp3", false},
    {"SCHLAGER R.", "Schlager Radio Berlin-Brandenburg", "http://addrad.io/4454rnz", false},
    {"RADIO TEDDY", "Radio Teddy", "http://streamtdy.ir-media-tec.com/live/mp3-128/web/play.mp3", false},
    {"94,3 RS2", "94,3 rs2", "http://stream.rs2.de/rs2/mp3-192", false},
    {"104.6 RTL", "104.6 RTL Hot Country", "http://stream.104.6rtl.com/hotcountry/mp3-192/", false},
    {"KISS FM", "Kiss Fm", "http://stream.kissfm.de/kissfm/mp3-192/internetradio", false},
    {"STAR FM 87.9", "Star Fm 87.9", "http://stream.starfm.de/berlin/mp3-192/iradio", false},

    // Überregional / International
    {"DEFJAY", "DEFJAY", "http://icepool.silvacast.com/DEFJAY.mp3", false},
    {"RADIO PALOMA", "Radio Paloma", "http://www3.radiopaloma.de/RP-Hauptkanal.pls", false},
    {"PARADISO", "Radio Paradiso Berlin", "https://streams.paradiso.de/Berlin/mp3-192/radiode/", false},
    {"JAM FM", "JAM FM Berlin", "http://stream.jam.fm/jamfm-nmr/mp3-128", false},
    {"COSMO", "Cosmo", "http://dispatcher.rndfnk.com/wdr/cosmo/live/mp3/128/stream.mp3", false},
    {"DLF", "Deutschlandfunk", "http://st01.sslstream.dlf.de/dlf/01/high/aac/stream.aac?aggregator=web", false},
    {"DLF KULTUR", "Deutschlandfunk Kultur", "http://st02.sslstream.dlf.de/dlf/02/high/aac/stream.aac?aggregator=web", false},
    {"DLF NOVA", "Deutschlandfunk Nova", "http://st03.sslstream.dlf.de/dlf/03/high/aac/stream.aac?aggregator=web", false},
    {"DETECTOR.FM", "detector.fm", "http://streams.detektor.fm/wort/mp3-256/website/", true},
    {"SOMAFM", "SomaFM", "http://ice1.somafm.com/groovesalad-128-mp3", false},

    // SWR / International
    {"SWR3", "SWR3", "http://liveradio.swr.de/sw282p3/swr3/play.mp3", true},
    {"KEXP", "KEXP", "http://live-mp3-128.kexp.org/kexp128.mp3", true},
    {"FIP", "FIP", "http://direct.fipradio.fr/live/fip-midfi.mp3", true},

    // Radio Paradise
    {"PARAD. MAIN", "Radio Paradise Main Mix", "http://stream.radioparadise.com/mp3-192", true},
    {"PARAD. MELLOW", "Radio Paradise Mellow Mix", "http://stream.radioparadise.com/mellow-192", false},
    {"PARAD. ROCK", "Radio Paradise Rock Mix", "http://stream.radioparadise.com/rock-192", false},
    {"PARAD. GLOBAL", "Radio Paradise Global Mix", "http://stream.radioparadise.com/global-192", false},
    {"PARAD. R.2050", "Radio Paradise radio 2050", "http://stream.radioparadise.com/radio2050-192", false},

    // FluxFM
    {"FLUXFM", "FluxFM", "http://streams.fluxfm.de/live/mp3-320/audio/", false},
    {"FLUX 60S", "Flux 60s", "http://streams.fluxfm.de/60er/mp3-320/streams.fluxfm.de/", false},
    {"FLUX 70S", "Flux 70s", "http://streams.fluxfm.de/70er/mp3-320/audio/", false},
    {"FLUX 80S", "Flux 80s", "http://streams.fluxfm.de/80er/mp3-320/streams.fluxfm.de/", true},
    {"FLUX 90S", "Flux 90s", "http://streams.fluxfm.de/90er/mp3-320/audio/", false},
    {"FLUX KLUBR.", "Flux Klubradio", "http://streams.fluxfm.de/klubradio/mp3-320/audio/", false},
    {"FLUX BOOM", "Flux BoomFM", "http://streams.fluxfm.de/boomfm/mp3-320/audio/", false},
    {"FLUX METAL", "Flux MetalFM", "http://streams.fluxfm.de/metalfm/mp3-320/radiode/", false},
    {"FLUXLOUNGE", "FluxLounge", "http://streams.fluxfm.de/lounge/mp3-320/audio/", false},
    {"FLUX NEO", "Flux neoFM", "http://streams.fluxfm.de/neofm/mp3-320/radiode/", false},
    {"F.KOMPENS.", "FluxKompensator", "http://streams.fluxfm.de/fluxkompensator/mp3-320/audio/", false},
    {"CLUBSANDW.", "Clubsandwich", "http://streams.fluxfm.de/clubsandwich/mp3-320/audio/", false},
    {"SOUND OF BLN", "Sound Of Berlin", "http://streams.fluxfm.de/soundofberlin/mp3-320/audio/", false},
    {"MINI FLUX", "MiniFlux", "http://streams.fluxfm.de/kinder1/mp3-320/streams.fluxfm.de/", false},
    {"ELEKTROFLUX", "ElektroFlux", "http://streams.fluxfm.de/elektro/mp3-320/audio/", false},
    {"YOGA SOUNDS", "Yoga Sounds", "http://streams.fluxfm.de/yogasounds/mp3-320/streams.fluxfm.de/", false},
    {"RASTA RADIO", "Rasta Radio", "http://streams.fluxfm.de/rastaradio/mp3-320/streams.fluxfm.de/", false},
    {"BLN BOHÈME", "Berlin Bohème", "http://streams.fluxfm.de/berlinboheme/mp3-320/streams.fluxfm.de/", false},
    {"CHILLOUT", "Chillout Radio", "http://streams.fluxfm.de/chillout/mp3-320/streams.fluxfm.de/", false},

    // Absolut Radio
    {"ABS. HOT", "Absolut Hot", "http://absolut-hot.live-sm.absolutradio.de/absolut-hot/stream/mp3", false},
    {"ABS. GERM.", "Absolut Germany", "http://absolut-germany.live-sm.absolutradio.de/absolut-germany/stream/mp3", false},
    {"ABS. TOP", "Absolut Top", "http://absolut-top.live-sm.absolutradio.de/absolut-top/stream/mp3", false},
    {"ABS. RELAX", "Absolut Relax", "http://absolut-relax.live-sm.absolutradio.de/absolut-relax/stream/mp3", false},
    {"ABS. BELLA", "Absolut Bella", "http://absolut-bella.live-sm.absolutradio.de/absolut-bella/stream/mp3", false},
    {"ABS. OLDIE", "Absolut Oldie Classics", "http://absolut-oldieclassics.live-sm.absolutradio.de/absolut-oldieclassics/stream/mp3", false},
    {"ABS. CLUB", "Absolut Top Clubnight", "http://absolut-clubnight.live-sm.absolutradio.de/absolut-clubnight/stream/mp3", false},
    {"ABS. 80ER", "Absolut 80er", "http://absolut-80er.live-sm.absolutradio.de/absolut-80er/stream/mp3", true},
    {"ABS. LOVE", "Absolut Lovesongs", "http://absolut-lovesongs.live-sm.absolutradio.de/absolut-lovesongs/stream/mp3", false},
    {"ABS. MUSICXL", "Absolut MusicXL", "http://absolut-musicxl.live-sm.absolutradio.de/absolut-musicxl/stream/mp3", false},
    {"ABS. COFFEE", "Absolut Coffeemusic", "http://absolut-coffeemusic.live-sm.absolutradio.de/absolut-coffeemusic/stream/mp3", false},
    {"ABS. ROCK", "Absolut ROCK", "http://absolut-rock.live-sm.absolutradio.de/absolut-rock/stream/mp3", false},
    {"ABS. AI", "Absolut Radio AI", "http://absolut-ai.live-sm.absolutradio.de/absolut-ai/stream/mp3", false},
};

/// Anzahl der Default-Sender (Konstante zur Übersetzungszeit)
static constexpr int DEFAULT_STATION_COUNT = sizeof(DEFAULT_STATIONS) / sizeof(DEFAULT_STATIONS[0]);

// Prüfung zur Übersetzungszeit: genau 70 Sender erwartet
static_assert(DEFAULT_STATION_COUNT == 70, "DEFAULT_STATIONS muss genau 70 Einträge enthalten!");

#endif // STATIONS_DEFAULT_H
