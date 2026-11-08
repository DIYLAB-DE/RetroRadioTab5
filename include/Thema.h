#ifndef THEMA_H
#define THEMA_H

#include <cstddef>
#include <cstdint>

/**
 * @brief Farbrollen der Oberfläche in fester Reihenfolge, der Wert ist der Index in den Farbtabellen der Themen.
 *
 * Der Bezeichner ist der Rollenname ohne Punkt (Gem.Grund -> GemGrund). Berechnete Rollen und Tabellenrollen haben keinen Einzelwert, siehe rollenArt().
 */
enum class Rolle : uint8_t
{
    // Gemeinsame Rollen
    GemGrund, ///< Grund aller Ansichten und Dialoge, Löschfarbe, schwarzer Transparenzschlüssel
    GemFlaeche, ///< Arbeitsfläche hinter Inhalten (Panel, Playlist, Skala, Fußzeilen, Anzeigefelder, Analoguhr)
    GemText, ///< Haupttext (Listen, Dialoge, Meldungen, Reglerbeschriftung)
    GemTextGedaempft, ///< Tastenbeschriftung, zurückgenommener Text, Reglerwert, Trenner der Infozeile
    GemTextHell, ///< hervorgehobener und gewählter Text, Ziffern auf Tasten
    GemTextSilber, ///< helle Zweitschrift: Skalen- und Fußzeilentitel, Symbolringe, Diagrammbeschriftung
    GemTextSchwach, ///< Hinweis- und Zweitzeilentext der Playlist
    GemTitel, ///< Titelzeilen von Dialogen, Cover und Verzeichnisdialog
    GemDialogRahmen, ///< Rahmen aller Dialoge
    GemDialogTrenner, ///< Trennlinie unter Dialogtiteln
    GemDialogText, ///< Fließtext in Dialogen und Beschriftung der OK-Taste
    GemRahmen, ///< neutraler Rahmen, Linie und Reglerspur (Tasten, Fußzeilenlinien, Playlist gespielt)
    GemTastenFlaeche, ///< Füllung normaler Tasten
    GemTasteAus, ///< Fläche und Rand deaktivierter Tasten
    GemTasteAusText, ///< Text deaktivierter Tasten
    GemAuswahl, ///< ausgewählte Zeile, aktive Taste
    GemAkzent, ///< kühler Hauptakzent: Symbole, Marker, Senderzeile, Reglermarke
    GemHinweis, ///< Bernstein: Hinweis, Pfad, Korrelation mittel
    GemWarnung, ///< Retro-Rot: Stopp, Fehler, Abschalten, Fortschrittsmarke
    GemScrollSpur, ///< Scrollbar-Spur
    GemScrollDaumen, ///< Scrollbar-Daumen
    GemScrollDaumenGezogen, ///< Scrollbar-Daumen beim Ziehen
    GemTeiler, ///< Trennbalken zwischen Skala, Fußzeile und Anzeige (Radio, SD-Player)
    GemSymbolAus, ///< Symbolvordergrund deaktivierter Tasten und Symbole
    GemReglerAktiv, ///< aktiver Bogen des Lautstärkereglers
    GemReglerNull, ///< Nulllinie der Klangschieber
    GemAugenRing, ///< Außenring des Magischen Auges (Radio, Einstellungsvorschau)
    GemStummSymbol, ///< Stummsymbol im Lautstärkeregler
    GemHausSymbol, ///< Haus-Symbol für die Rückkehr ins Hauptmenü (Taste unten rechts, Sleep-Timer), im Original die Farbe der AUS-Taste des Hauptmenüs
    // Uhren (Radio, SD-Player und Fußzeile der Einstellungen)
    UhrTimerBogen, ///< Schlaftimer-Bogen der Uhren
    UhrTimerZahl, ///< Restzeitzahlen des Timers
    UhrAnalogMarken, ///< Stundenmarken der Analoguhr
    UhrAnalogZeiger, ///< Zeiger und Ziffern 12/3/6/9 der Analoguhr
    UhrSekunde, ///< Sekundenzeiger der Analoguhr
    UhrDigitalZiffern, ///< Ziffern der Digitaluhr
    UhrDigitalSpur, ///< Geisterziffern 88:88, Doppelpunkt aus und Timerspur der Digitaluhr
    // MainScreen
    MainUhrMarken, ///< Stundenmarken 12, 3, 6, 9
    MainUhrMarkenKlein, ///< Minutenmarken und übrige Stundenmarken
    MainUhrZeiger, ///< Stunden- und Minutenzeiger
    MainUhrSekunde, ///< Sekundenzeiger
    MainUhrAchse, ///< Achse der Zeiger
    MainUhrAchseAkzent, ///< Achse des Sekundenzeigers (Mitte der Analoguhr)
    MainStatusNtp, ///< NTP-Marker aktiv (11 Uhr)
    MainStatusDst, ///< Sommerzeit-Marker aktiv (1 Uhr)
    MainStatusWlan, ///< WLAN-Balken aktiv
    MainStatusAus, ///< inaktive Statusanzeigen
    MainStatusText, ///< Beschriftung NTP/DST
    MainDatum, ///< Datumszeile
    MainSsid, ///< SSID unter den WLAN-Balken
    MainIp, ///< IP-Adresse
    MainTasteRahmen, ///< Rahmen der drei Symboltasten
    MainTasteFlaeche, ///< Füllung der Symboltasten
    MainTasteText, ///< Rahmen der gedrückten Symboltaste (Tastentext)
    MainSymbolRadio, ///< Tönung des Radio-Symbols
    MainSymbolSd, ///< Tönung des SD-Player-Symbols
    MainSymbolKonfig, ///< Tönung des Konfigurationssymbols
    // Sperrbildschirm
    SperreTasteRahmen, ///< Rahmen der PIN-Tasten
    SperreTasteGedrueckt, ///< Füllung der gedrückten PIN-Taste
    SperreTasteText, ///< Ziffern der PIN-Tasten
    SperreTasteLoeschen, ///< Beschriftung der Löschtaste
    SperrePulsLoeschen, ///< Rahmenpuls der Löschtaste
    SperreTasteOk, ///< Beschriftung der OK-Taste
    SperrePulsFehler, ///< Rahmenpuls bei falscher PIN
    // Radio
    RadSenderMarke, ///< Marker normaler Sender auf der Skala
    RadFavorit, ///< Marker und Unterstreichung der Favoriten auf der Skala
    RadBitrate, ///< Bitrate in der Infozeile
    RadStereoAus, ///< Stereo-Symbol bei Mono
    RadNadel, ///< rote Abstimmnadel der Skala
    RadStern, ///< Favoritenstern in der Tastenleiste
    // SD-Player
    SDTitelAktiv, ///< Zweitzeile der gewählten Playlist-Zeile
    SDTitelGespielt, ///< bereits gespielte Titel der Playlist
    SDFortschrittGrund, ///< Grund der Fortschrittsanzeige
    SDFortschrittBalken, ///< gespielter Anteil der Fortschrittsanzeige
    SDFortschrittLinie, ///< rote Positionslinie der Fortschrittsanzeige
    SDDetailGrund, ///< Grund der Detailzeile unter dem Cover
    SDBadgeMp3, ///< Format-Marke MP3
    SDBadgeFlac, ///< Format-Marke FLAC
    SDBadgeOgg, ///< Format-Marke OGG
    SDBadgeAac, ///< Format-Marke AAC/M4A und andere
    SDBadgeText, ///< Schrift auf den Format-Marken
    SDBadgeBitrate, ///< Marke der Bitrate
    SDPlayLaeuft, ///< Play-Symbol während der Wiedergabe
    SDPlayWiederholTitel, ///< Play-Symbol bei Wiederholung des Titels
    SDPlayWiederholListe, ///< Play-Symbol bei Wiederholung der Playlist
    SDPausePuls, ///< Puls des Pause-Symbols (Mischung Weiß/Fläche)
    SDCoverText, ///< Dateiname auf dem Cover
    SDCoverInitialen, ///< Initialen auf dem Platzhalter-Cover
    SDOrdnerLeer, ///< Ordnersymbol der leeren Playlist
    SDNabeRing, ///< Öffnungsgrund außen der Kassettennabe
    SDNabeDunkel, ///< dunkle Nabenfläche
    SDNabeZahnkranz, ///< Zahnkranz der Nabe
    SDNabeAchse, ///< Dornstift der Nabe
    SDTransparenz, ///< Transparenzschlüssel des Naben-Sprites
    SDKassetteEtikettText, ///< Schrift auf dem Kassettenetikett (je Kassette)
    SDKassetteBand, ///< Bandwickel im Kassettenfenster (je Kassette)
    SDKassetteBandKante, ///< Kante des Bandwickels (je Kassette)
    // Einstellungen
    EinText, ///< Standardtext, Ziffern der Zifferntasten
    EinTextNeben, ///< Untertitel und Hilfetext
    EinSektionKopfText, ///< Text der Sektionsköpfe im Menü
    EinTastenText, ///< Text normaler Tasten
    EinTitelFeld, ///< Grund der Titelbox
    EinTrenner, ///< Trennlinie zwischen Listeneinträgen und Menüpunkten
    EinZeile, ///< Grund einer Zeile, Picker
    EinZeileGewaehlt, ///< Grund der gewählten Zeile, Picker-Auswahl
    EinPunktLeer, ///< leerer PIN-Punkt
    EinSektionVerbindung, ///< Kategoriefarbe Verbindung (Streifen, Menü)
    EinSektionSystem, ///< Kategoriefarbe System, Haken, Titel- und Statusgrün
    EinSektionAbgeleitet, ///< aus der Kategoriefarbe berechnete Töne (Menügrund, Streifen, I2C-Marke)
    EinStatusInaktiv, ///< WLAN-Profil inaktiv
    EinStatusLeer, ///< WLAN-Profil ohne Netz
    EinSchalterAn, ///< Schalterknopf an (Oliv)
    EinSchalterGehaeuseAus, ///< Gehäuse des ausgeschalteten Schalters
    EinPanelTitel, ///< Titel der Einstellungsseite
    EinInfoKopfNetz, ///< Kopf des Netzwerkblocks der Infoseite
    EinInfoKopfSystem, ///< Kopf des Systemblocks der Infoseite
    EinSignalMittel, ///< Signalbalken mittel
    EinSignalSchwach, ///< Signalbalken schwach
    EinPunkteAn, ///< leuchtende Punkte der Lautstärkezahl
    EinPunkteAus, ///< dunkle Punkte der Lautstärkezahl
    EinStopSymbol, ///< Stopp-Symbol der Hörprobe
    EinPlaySymbol, ///< Play-Symbol der Hörprobe
    EinNumTaste, ///< Zifferntaste des Zahlenblocks
    EinNumZurueck, ///< Rücktaste des Zahlenblocks
    EinNumLoeschen, ///< Löschtaste des Zahlenblocks
    EinNumGedrueckt, ///< gedrückte Taste des Zahlenblocks
    // Dialoge
    DlgTasteZweitzeichen, ///< Beschriftung der Zweitebene auf Tastaturtasten
    DlgTaste, ///< Buchstaben- und Zeichentaste der Tastatur
    DlgTasteAktiv, ///< aktive Umschalttaste (Shift, Caps)
    DlgTasteSonder, ///< Sondertaste der Tastatur, Auge-Taste
    DlgEingabeFeld, ///< Eingabefeld der Tastatur
    DlgCursor, ///< Texteinfügemarke der Tastatur
    DlgSchliessen, ///< Schließen-Taste
    DlgNetzStark, ///< Netz in der Suchliste: stark
    DlgNetzMittel, ///< Netz in der Suchliste: mittel
    DlgNetzSchwach, ///< Netz in der Suchliste: schwach
    DlgListenTrenner, ///< Trennlinie der Netzliste
    DlgListeScrollDaumen, ///< Scrollbar-Daumen der Netzliste
    DlgFortschrittBalken, ///< aktives Segment des Fortschrittsdialogs
    DlgFortschrittKappe, ///< Kappe des Fortschrittsbalkens
    DlgFortschrittGrund, ///< Grund des Fortschrittsbalkens
    DlgFavoritTasteRahmen, ///< Rahmen der Favoritentasten
    DlgFavoritZahlFlaeche, ///< Zahlenkreis der Favoritentasten
    DlgFavoritZahlText, ///< Ziffer im Zahlenkreis
    DlgExplorerOrdner, ///< Ordnereinträge und Ordnersymbol im Verzeichnisdialog
    DlgExplorerMp3, ///< Typangabe .mp3 im Verzeichnisdialog
    DlgExplorerFlac, ///< Typangabe .flac im Verzeichnisdialog
    // Visualizer
    VisNadelKlassisch, ///< Zeiger des Analog-VU auf dem klassischen Blatt
    VisNadelGruen, ///< Zeiger des Analog-VU auf dem grünen Blatt
    VisPegelGruen, ///< Pegelfarbe normal: RMS-Balken, Spektren, Wasserfall klassisch, Gonio-VU
    VisPegelOrange, ///< Pegelfarbe hoch: RMS, Gonio-VU, Schwelle 1
    VisPegelRot, ///< Pegelfarbe Übersteuerung: RMS, PPM, Gonio-VU, Schwelle 2, Skalennull
    VisSegmentLuecke, ///< schwarze Lücken zwischen den Segmenten der Pegelbalken
    VisKorrelationMitte, ///< Mittelmarke der Korrelationsleiste
    VisPegelGrund, ///< Grund der Pegelbalken und Segmentzeilen, Standby-Quadrate
    VisSpitze, ///< Spitzenmarke der Pegelbalken
    VisSpitzeSilber, ///< Spitzenmarke der Spektren
    VisPpmGruen, ///< PPM-Pegel normal
    VisPpmGelb, ///< PPM-Pegel hoch
    VisSkalaGross, ///< große Teilstriche der Pegelskala
    VisSkalaKlein, ///< kleine Teilstriche der Pegelskala
    VisSkalaLinie, ///< Hilfslinien der Gonio-VU-Skala
    VisRaster, ///< Gitter, Fadenkreuz, Korrelationsleiste und Rahmen des Goniometers und der Wellenform
    VisRegenbogen, ///< Regenbogenskala 192 Werte (Spektren, RMS, Goniometer, Gonio-VU)
    VisWasserfallSkala, ///< Farbstufen des Wasserfalls von Stille bis Maximum
    VisWelleLinks, ///< Wellenform linker Kanal
    VisWelleRechts, ///< Wellenform rechter Kanal
    VisGonioPunkt, ///< Goniometerpunkte in jeder Fassung
    VisGonioKorrelation, ///< Korrelationsmarke: gut
    VisGonioInfoText, ///< Text im Gonio-Infofeld
    VisGonioInfoPeak, ///< PEAK-Beschriftung im Infofeld
    VisGonioInfoMarke, ///< Linien unter den Kanalnamen
    VisGonioInfoTitel, ///< Korrelationstitel im Infofeld
    VisAuroraKalt, ///< kühle Farbe der drei Nordlichtschleier
    VisAuroraWarm, ///< warme Farbe der drei Nordlichtschleier
    // Magisches Auge
    AugPhosphor, ///< Phosphorgrün, Randlicht, Kantenlicht und Kappe des Magischen Auges
    Anzahl ///< Anzahl der Rollen, keine Rolle
};

constexpr size_t RollenAnzahl = static_cast<size_t>(Rolle::Anzahl); ///< Anzahl der Rollen

/** @brief Art einer Rolle. */
enum class RollenArt : uint8_t
{
    Farbe,     ///< Ein RGB565-Wert in Thema::farbe
    Berechnet, ///< Aus anderen Rollen errechnete Töne, ohne eigenen Wert
    Tabelle    ///< Reihe in Farbtabellen statt eines Einzelwerts
};

/** @brief Liefert die Art einer Rolle. */
constexpr RollenArt rollenArt(Rolle rolle)
{
    switch (rolle)
    {
    case Rolle::SDPausePuls:
    case Rolle::EinSektionAbgeleitet:
        return RollenArt::Berechnet;
    case Rolle::SDKassetteEtikettText:
    case Rolle::SDKassetteBand:
    case Rolle::SDKassetteBandKante:
    case Rolle::VisRegenbogen:
    case Rolle::VisWasserfallSkala:
    case Rolle::VisAuroraKalt:
    case Rolle::VisAuroraWarm:
    case Rolle::AugPhosphor:
        return RollenArt::Tabelle;
    default:
        return RollenArt::Farbe;
    }
}

/** @brief Merkmale eines Themas als Bitmaske in Thema::merkmale. */
enum ThemaMerkmal : uint8_t
{
    MerkmalGraustufen = 1,  ///< Bilder, Cover und Paletten zur Laufzeit in Grau umrechnen
    MerkmalHellerGrund = 2, ///< Grund heller als die Zeichnung: Transparenzschlüssel, Wasserfall-Null und Nordlicht-Mischung umkehren
    MerkmalAkzentarm = 4    ///< Rollen ohne E-Paper-Akzent nur als Grautöne zulassen
};

/** @brief Herkunft der Bilder eines Themas. */
enum class Bildsatz : uint8_t
{
    Retro,        ///< Die heutigen Bilder unverändert
    GrauLaufzeit, ///< Die heutigen Bilder, zur Laufzeit in Grau umgerechnet
    Bauzeit       ///< Eigene, zur Bauzeit erzeugte Bilder
};

/** @brief Koeffizienten des Magischen Auges (Aug.Phosphor): Helligkeiten im Bereich 0..255, Farbanteile 0..1 je Kanal. */
struct AugenKoeffizienten
{
    float helligkeitLeuchten;                       ///< Helligkeit des leuchtenden Phosphors
    float helligkeitSchatten;                       ///< Restleuchten außerhalb der Leuchtflächen
    float helligkeitRand;                           ///< Helligkeit des gelbgrünen Randbogens
    float helligkeitKante;                          ///< Helligkeit des Kantenlichts an den Hauptkanten
    float helligkeitNebenkante;                     ///< Helligkeit des Kantenlichts an den Nebenkeilen der EM34
    float helligkeitEcke;                           ///< Helligkeit der Eckpunkte am Schirmrand
    float helligkeitMax;                            ///< Obergrenze der Helligkeitsrechnung
    float phosphorRot, phosphorGruen, phosphorBlau; ///< Farbanteile des Phosphorgrüns
    float randRot, randGruen, randBlau;             ///< Farbanteile des gelbgrünen Randlichts
    float tonKante;                                 ///< Gelbanteil des Kantenlichts an den Hauptkanten
    float tonEcke;                                  ///< Gelbanteil der Eckpunkte
    float tonNebenkante;                            ///< Gelbanteil des Kantenlichts an den Nebenkeilen
    float kappeHell;                                ///< Helligkeit der Metallkappe in ihrer Mitte
    float kappeDunkel;                              ///< Helligkeit der Metallkappe an ihrem Rand
    float kappeGruen, kappeBlau;                    ///< Grün- und Blauanteil des warm getönten Metallgraus
};

/** @brief Reihen der Tabellenrollen eines Themas. */
struct Farbtabellen
{
    uint16_t regenbogen[192];                                               ///< Vis.Regenbogen: zyklischer RGB565-Verlauf
    uint16_t wasserfall[6];                                                 ///< Vis.WasserfallSkala: Stufen von Stille bis Maximum
    uint8_t auroraKalt[3][3], auroraWarm[3][3];                             ///< Vis.AuroraKalt und Vis.AuroraWarm: RGB je Nordlichtschleier
    uint16_t kassetteEtikettText[8], kassetteBand[8], kassetteBandKante[8]; ///< SD.KassetteEtikettText, SD.KassetteBand, SD.KassetteBandKante je Kassette
    AugenKoeffizienten augePhosphor;                                        ///< Aug.Phosphor: Farb- und Helligkeitskoeffizienten des Magischen Auges
};

/** @brief Ein Oberflächenthema, wird einmal beim Start gewählt und gilt bis zum Neustart. */
struct Thema
{
    const char *name;               ///< Name im Log, die Themenseite der Einstellungen zeigt übersetzte Namen
    uint16_t farbe[RollenAnzahl];   ///< RGB565 je Rolle, 0 bei berechneten und Tabellenrollen
    const Farbtabellen *tabellen;   ///< Reihen der Tabellenrollen, nie nullptr
    Bildsatz bildsatz;              ///< Herkunft der Bilder
    uint8_t merkmale;               ///< Bitmaske aus ThemaMerkmal
};

constexpr uint8_t ThemaRetroIndex = 0;  ///< Gespeicherter Index von Retro, zugleich Rückfall für unbekannte Indizes
constexpr uint8_t ThemaModernIndex = 1; ///< Gespeicherter Index von Modern. Dem Benutzer heißt es „Individuell“ (englisch „Custom“) und steht in Menü und Webportal hinter E-Paper, weil es frei einstellbar ist
constexpr uint8_t ThemaEPaperIndex = 2; ///< Gespeicherter Index von E-Paper
constexpr uint8_t ThemenAnzahl = 3;     ///< Anzahl der Themenindizes

/** @brief Rolle und RGB565-Wert für den Aufbau eines Themas. */
struct RollenWert
{
    Rolle rolle;   ///< Rolle mit RollenArt::Farbe
    uint16_t wert; ///< RGB565
};

/** @brief Retro-Werte aller Rollen mit Einzelwert: die Farben des Originals, jede Rolle einzeln, damit Retro pixelgleich bleibt. */
inline constexpr RollenWert RetroWerte[] = {
    {Rolle::GemGrund, 0x0000},
    {Rolle::GemFlaeche, 0x1082},
    {Rolle::GemText, 0xD69A},
    {Rolle::GemTextGedaempft, 0x7BEF},
    {Rolle::GemTextHell, 0xFFFF},
    {Rolle::GemTextSilber, 0xC618},
    {Rolle::GemTextSchwach, 0x5ACB},
    {Rolle::GemTitel, 0xEC84},
    {Rolle::GemDialogRahmen, 0x24F2},
    {Rolle::GemDialogTrenner, 0xD206},
    {Rolle::GemDialogText, 0xBDF7},
    {Rolle::GemRahmen, 0x39E8},
    {Rolle::GemTastenFlaeche, 0x0000},
    {Rolle::GemTasteAus, 0x2104},
    {Rolle::GemTasteAusText, 0x5AEB},
    {Rolle::GemAuswahl, 0x328A},
    {Rolle::GemAkzent, 0x5370},
    {Rolle::GemHinweis, 0xED86},
    {Rolle::GemWarnung, 0xD206},
    {Rolle::GemScrollSpur, 0x2104},
    {Rolle::GemScrollDaumen, 0x5ACB},
    {Rolle::GemScrollDaumenGezogen, 0x8410},
    {Rolle::GemTeiler, 0x79E2},
    {Rolle::GemSymbolAus, 0xC618},
    {Rolle::GemReglerAktiv, 0x867D},
    {Rolle::GemReglerNull, 0x7BEF},
    {Rolle::GemAugenRing, 0x1924},
    {Rolle::GemStummSymbol, 0x9CF3},
    {Rolle::GemHausSymbol, 0x98E3},
    {Rolle::UhrTimerBogen, 0x8010},
    {Rolle::UhrTimerZahl, 0xDB9A},
    {Rolle::UhrAnalogMarken, 0x528A},
    {Rolle::UhrAnalogZeiger, 0xC618},
    {Rolle::UhrSekunde, 0xF800},
    {Rolle::UhrDigitalZiffern, 0x7BEF},
    {Rolle::UhrDigitalSpur, 0x2104},
    {Rolle::MainUhrMarken, 0xDE54},
    {Rolle::MainUhrMarkenKlein, 0x4A08},
    {Rolle::MainUhrZeiger, 0xF718},
    {Rolle::MainUhrSekunde, 0xF800},
    {Rolle::MainUhrAchse, 0x0000},
    {Rolle::MainUhrAchseAkzent, 0xF800},
    {Rolle::MainStatusNtp, 0x5C44},
    {Rolle::MainStatusDst, 0xA285},
    {Rolle::MainStatusWlan, 0xDE54},
    {Rolle::MainStatusAus, 0x4A08},
    {Rolle::MainStatusText, 0xDE54},
    {Rolle::MainDatum, 0x9C2B},
    {Rolle::MainSsid, 0xDE54},
    {Rolle::MainIp, 0x7D7A},
    {Rolle::MainTasteRahmen, 0x5A89},
    {Rolle::MainTasteFlaeche, 0x0000},
    {Rolle::MainTasteText, 0xDE54},
    {Rolle::MainSymbolRadio, 0x9C2B},
    {Rolle::MainSymbolSd, 0x9C2B},
    {Rolle::MainSymbolKonfig, 0x42CA},
    {Rolle::SperreTasteRahmen, 0x228E},
    {Rolle::SperreTasteGedrueckt, 0x228E},
    {Rolle::SperreTasteText, 0xDE54},
    {Rolle::SperreTasteLoeschen, 0xFD20},
    {Rolle::SperrePulsLoeschen, 0xFD20},
    {Rolle::SperreTasteOk, 0x07E0},
    {Rolle::SperrePulsFehler, 0xF800},
    {Rolle::RadSenderMarke, 0xE300},
    {Rolle::RadFavorit, 0x6C64},
    {Rolle::RadBitrate, 0x6C64},
    {Rolle::RadStereoAus, 0x6B4D},
    {Rolle::RadNadel, 0xF800},
    {Rolle::RadStern, 0xFC60},
    {Rolle::SDTitelAktiv, 0xFC60},
    {Rolle::SDTitelGespielt, 0x39E8},
    {Rolle::SDFortschrittGrund, 0x2104},
    {Rolle::SDFortschrittBalken, 0x4208},
    {Rolle::SDFortschrittLinie, 0xD206},
    {Rolle::SDDetailGrund, 0x2104},
    {Rolle::SDBadgeMp3, 0x5370},
    {Rolle::SDBadgeFlac, 0x07E0},
    {Rolle::SDBadgeOgg, 0xF81F},
    {Rolle::SDBadgeAac, 0x7BEF},
    {Rolle::SDBadgeText, 0x0000},
    {Rolle::SDBadgeBitrate, 0x6C64},
    {Rolle::SDPlayLaeuft, 0xFD20},
    {Rolle::SDPlayWiederholTitel, 0x07E0},
    {Rolle::SDPlayWiederholListe, 0xF800},
    {Rolle::SDCoverText, 0xED86},
    {Rolle::SDCoverInitialen, 0xD206},
    {Rolle::SDOrdnerLeer, 0x7BEF},
    {Rolle::SDNabeRing, 0x18C3},
    {Rolle::SDNabeDunkel, 0x1082},
    {Rolle::SDNabeZahnkranz, 0xCE79},
    {Rolle::SDNabeAchse, 0x632C},
    {Rolle::SDTransparenz, 0x0020},
    {Rolle::EinText, 0xDEDB},
    {Rolle::EinTextNeben, 0xB5F7},
    {Rolle::EinSektionKopfText, 0xB5F7},
    {Rolle::EinTastenText, 0xAD75},
    {Rolle::EinTitelFeld, 0x2104},
    {Rolle::EinTrenner, 0x2945},
    {Rolle::EinZeile, 0x18C3},
    {Rolle::EinZeileGewaehlt, 0x2945},
    {Rolle::EinPunktLeer, 0x4208},
    {Rolle::EinSektionVerbindung, 0x07F4},
    {Rolle::EinSektionSystem, 0x07E0},
    {Rolle::EinStatusInaktiv, 0x8410},
    {Rolle::EinStatusLeer, 0x6B4D},
    {Rolle::EinSchalterAn, 0x5345},
    {Rolle::EinSchalterGehaeuseAus, 0x7BEF},
    {Rolle::EinPanelTitel, 0x3666},
    {Rolle::EinInfoKopfNetz, 0x60A6},
    {Rolle::EinInfoKopfSystem, 0x6A82},
    {Rolle::EinSignalMittel, 0xFE60},
    {Rolle::EinSignalSchwach, 0xF800},
    {Rolle::EinPunkteAn, 0x7BEF},
    {Rolle::EinPunkteAus, 0x2104},
    {Rolle::EinStopSymbol, 0xA944},
    {Rolle::EinPlaySymbol, 0x5C44},
    {Rolle::EinNumTaste, 0x2945},
    {Rolle::EinNumZurueck, 0x3ACF},
    {Rolle::EinNumLoeschen, 0x7800},
    {Rolle::EinNumGedrueckt, 0x6B6D},
    {Rolle::DlgTasteZweitzeichen, 0xFEA0},
    {Rolle::DlgTaste, 0x4A69},
    {Rolle::DlgTasteAktiv, 0x4C16},
    {Rolle::DlgTasteSonder, 0x3986},
    {Rolle::DlgEingabeFeld, 0x3986},
    {Rolle::DlgCursor, 0x07FF},
    {Rolle::DlgSchliessen, 0xF800},
    {Rolle::DlgNetzStark, 0x07E0},
    {Rolle::DlgNetzMittel, 0xFFE0},
    {Rolle::DlgNetzSchwach, 0xF800},
    {Rolle::DlgListenTrenner, 0x2104},
    {Rolle::DlgListeScrollDaumen, 0x4A49},
    {Rolle::DlgFortschrittBalken, 0x7BEF},
    {Rolle::DlgFortschrittKappe, 0x3186},
    {Rolle::DlgFortschrittGrund, 0x2104},
    {Rolle::DlgFavoritTasteRahmen, 0x0000},
    {Rolle::DlgFavoritZahlFlaeche, 0x39E8},
    {Rolle::DlgFavoritZahlText, 0xFFFF},
    {Rolle::DlgExplorerOrdner, 0x8DFA},
    {Rolle::DlgExplorerMp3, 0x3666},
    {Rolle::DlgExplorerFlac, 0xFFE0},
    {Rolle::VisNadelKlassisch, 0xF800},
    {Rolle::VisNadelGruen, 0xFD87},
    {Rolle::VisPegelGruen, 0x6C64},
    {Rolle::VisPegelOrange, 0xFDA0},
    {Rolle::VisPegelRot, 0xF800},
    {Rolle::VisSegmentLuecke, 0x0000},
    {Rolle::VisKorrelationMitte, 0xFEA0},
    {Rolle::VisPegelGrund, 0x2104},
    {Rolle::VisSpitze, 0xFFFF},
    {Rolle::VisSpitzeSilber, 0xC618},
    {Rolle::VisPpmGruen, 0x5370},
    {Rolle::VisPpmGelb, 0x5370},
    {Rolle::VisSkalaGross, 0xC618},
    {Rolle::VisSkalaKlein, 0x4208},
    {Rolle::VisSkalaLinie, 0x7BEF},
    {Rolle::VisRaster, 0x2104},
    {Rolle::VisWelleLinks, 0x03E0},
    {Rolle::VisWelleRechts, 0x7BEF},
    {Rolle::VisGonioPunkt, 0x3666},
    {Rolle::VisGonioKorrelation, 0x3666},
    {Rolle::VisGonioInfoText, 0xD69A},
    {Rolle::VisGonioInfoPeak, 0xD206},
    {Rolle::VisGonioInfoMarke, 0x5370},
    {Rolle::VisGonioInfoTitel, 0xED86},
};

/** @brief Prüft, dass jede Rolle mit Einzelwert genau einmal und keine andere Rolle vorkommt. */
constexpr bool vollstaendig(const RollenWert *werte, size_t anzahl)
{
    for (size_t r = 0; r < RollenAnzahl; ++r)
    {
        size_t treffer = 0;
        for (size_t i = 0; i < anzahl; ++i)
            if (static_cast<size_t>(werte[i].rolle) == r)
                ++treffer;
        if (treffer != (rollenArt(static_cast<Rolle>(r)) == RollenArt::Farbe ? 1u : 0u))
            return false;
    }
    return true;
}

static_assert(vollstaendig(RetroWerte, sizeof(RetroWerte) / sizeof(RetroWerte[0])), "Retro muss jede Rolle mit Einzelwert genau einmal setzen");

/** @brief Reihen der Tabellenrollen von Retro. */
inline constexpr Farbtabellen RetroTabellen = {
    // Vis.Regenbogen: zyklischer Verlauf aus RetroRadio ea4229a34fb39faaff23619092cf09cafb8c51cf, Gfx.cpp
    {
        0xF800, 0xF820, 0xF840, 0xF860, 0xF880, 0xF8A0, 0xF8C0, 0xF8E0,
        0xF900, 0xF920, 0xF940, 0xF960, 0xF980, 0xF9A0, 0xF9C0, 0xF9E0,
        0xFA00, 0xFA20, 0xFA40, 0xFA60, 0xFA80, 0xFAA0, 0xFAC0, 0xFAE0,
        0xFB00, 0xFB20, 0xFB40, 0xFB60, 0xFB80, 0xFBA0, 0xFBC0, 0xFBE0,
        0xFBE0, 0xF3E0, 0xEBE0, 0xE3E0, 0xDBE0, 0xD3E0, 0xCBE0, 0xC3E0,
        0xBBE0, 0xB3E0, 0xABE0, 0xA3E0, 0x9BE0, 0x93E0, 0x8BE0, 0x83E0,
        0x7BE0, 0x73E0, 0x6BE0, 0x63E0, 0x5BE0, 0x53E0, 0x4BE0, 0x43E0,
        0x3BE0, 0x33E0, 0x2BE0, 0x23E0, 0x1BE0, 0x13E0, 0x0BE0, 0x03E0,
        0x03E0, 0x03E1, 0x03E2, 0x03E3, 0x03E4, 0x03E5, 0x03E6, 0x03E7,
        0x03E8, 0x03E9, 0x03EA, 0x03EB, 0x03EC, 0x03ED, 0x03EE, 0x03EF,
        0x03F0, 0x03F1, 0x03F2, 0x03F3, 0x03F4, 0x03F5, 0x03F6, 0x03F7,
        0x03F8, 0x03F9, 0x03FA, 0x03FB, 0x03FC, 0x03FD, 0x03FE, 0x03FF,
        0x03FF, 0x03DF, 0x03BF, 0x039F, 0x037F, 0x035F, 0x033F, 0x031F,
        0x02FF, 0x02DF, 0x02BF, 0x029F, 0x027F, 0x025F, 0x023F, 0x021F,
        0x01FF, 0x01DF, 0x01BF, 0x019F, 0x017F, 0x015F, 0x013F, 0x011F,
        0x00FF, 0x00DF, 0x00BF, 0x009F, 0x007F, 0x005F, 0x003F, 0x001F,
        0x001F, 0x081F, 0x101F, 0x181F, 0x201F, 0x281F, 0x301F, 0x381F,
        0x401F, 0x481F, 0x501F, 0x581F, 0x601F, 0x681F, 0x701F, 0x781F,
        0x801F, 0x881F, 0x901F, 0x981F, 0xA01F, 0xA81F, 0xB01F, 0xB81F,
        0xC01F, 0xC81F, 0xD01F, 0xD81F, 0xE01F, 0xE81F, 0xF01F, 0xF81F,
        0xF81F, 0xF81E, 0xF81D, 0xF81C, 0xF81B, 0xF81A, 0xF819, 0xF818,
        0xF817, 0xF816, 0xF815, 0xF814, 0xF813, 0xF812, 0xF811, 0xF810,
        0xF80F, 0xF80E, 0xF80D, 0xF80C, 0xF80B, 0xF80A, 0xF809, 0xF808,
        0xF807, 0xF806, 0xF805, 0xF804, 0xF803, 0xF802, 0xF801, 0xF800},
    // Vis.WasserfallSkala, Vis.AuroraKalt und Vis.AuroraWarm aus RetroRadio ea4229a34fb39faaff23619092cf09cafb8c51cf, Visualizer.cpp (COLOR_STOPS und LAYERS)
    {0x0000, 0x0010, 0x801F, 0xF800, 0xFFE0, 0xFFFF},
    {{22, 170, 136}, {22, 70, 162}, {112, 50, 145}},
    {{185, 92, 25}, {140, 45, 34}, {148, 76, 69}},
    // SD.KassetteEtikettText, SD.KassetteBand und SD.KassetteBandKante aus der Kassettentabelle von RetroRadio ea4229a34fb39faaff23619092cf09cafb8c51cf, SDPlayer.cpp, in der Reihenfolge von SdCassetteGeometries: Auralux, Modutape, Sunbeam, Ember, Glasstone, Auralite, Lumotape, Aurion
    {0x2124, 0x2124, 0x2124, 0x2124, 0x2965, 0x39E7, 0x2124, 0x2124},
    {0x3122, 0x3102, 0x5183, 0x3922, 0x3102, 0x1040, 0x3102, 0x3922},
    {0x4163, 0x3943, 0x69C3, 0x4963, 0x4143, 0x28E2, 0x3943, 0x4963},
    // Aug.Phosphor in der Reihenfolge von AugenKoeffizienten
    {185.0f, 46.0f, 165.0f, 72.0f, 52.0f, 92.0f, 255.0f, 0.11f, 1.00f, 0.26f, 0.60f, 1.00f, 0.07f, 0.88f, 0.95f, 0.72f, 75.0f, 30.0f, 0.98f, 0.93f}};

/** @brief Baut ein Thema aus Einzelwerten, nicht gesetzte Rollen bleiben 0. */
constexpr Thema baueThema(const char *name, const RollenWert *werte, size_t anzahl, const Farbtabellen *tabellen, Bildsatz bildsatz, uint8_t merkmale)
{
    Thema thema{name, {}, tabellen, bildsatz, merkmale};
    for (size_t i = 0; i < anzahl; ++i)
        thema.farbe[static_cast<size_t>(werte[i].rolle)] = werte[i].wert;
    return thema;
}

/** @brief Thema Retro, Index 0. */
inline constexpr Thema ThemaRetro = baueThema("Retro", RetroWerte, sizeof(RetroWerte) / sizeof(RetroWerte[0]), &RetroTabellen, Bildsatz::Retro, 0);

/** @brief Gruppe einer Grundfarbe von Modern, ordnet die bearbeitbaren Farben im Webportal. */
enum class ModernGruppe : uint8_t
{
    Fest,     ///< Grund und Fläche: nicht bearbeitbar, weil Logos, Kassetten und Cover sie eingebrannt tragen
    Akzente,  ///< Cyan, Blau, Violett, Magenta, Rot, Orange, Gelb, Gruen
    Text,     ///< Text, Silber, Gedaempft
    Erweitert ///< Ebene1, Ebene2, Linie, Weiss, CyanTief, ViolettTief, MagentaTief
};

/** @brief Grundfarbe von Modern als Index in ModernGrundfarben und ModernFarben::rgb, ab ModernErsteBearbeitbare zugleich die Reihenfolge der Farben in rr_tab5/modern_v1. */
enum class ModernFarbe : uint8_t
{
    Grund,       ///< Grund aller Ansichten
    Flaeche,     ///< Arbeitsfläche
    Cyan,        ///< Hauptakzent
    Blau,        ///< Zweitakzent
    Violett,     ///< Kategorie System
    Magenta,     ///< Gegenakzent
    Rot,         ///< Warnung und Stopp
    Orange,      ///< Pegel hoch
    Gelb,        ///< Hinweis
    Gruen,       ///< Bestätigung
    Text,        ///< Haupttext
    Silber,      ///< Zweitschrift
    Gedaempft,   ///< gedämpfter Text
    Ebene1,      ///< erhöhte Fläche
    Ebene2,      ///< zweite Ebene
    Linie,       ///< Rahmen und Linien
    Weiss,       ///< hervorgehobener Text
    CyanTief,    ///< Auswahlfläche
    ViolettTief, ///< Sondertasten
    MagentaTief, ///< Löschtaste des Zahlenblocks
    Anzahl       ///< Anzahl der Grundfarben, keine Farbe
};

constexpr size_t ModernFarbenAnzahl = static_cast<size_t>(ModernFarbe::Anzahl);           ///< Anzahl der Grundfarben von Modern
constexpr size_t ModernErsteBearbeitbare = static_cast<size_t>(ModernFarbe::Cyan);        ///< Index der ersten bearbeitbaren Grundfarbe, Grund und Fläche davor sind fest
constexpr size_t ModernBearbeitbarAnzahl = ModernFarbenAnzahl - ModernErsteBearbeitbare; ///< Anzahl der bearbeitbaren Grundfarben

/** @brief Name, Vorgabe und Gruppe einer Grundfarbe von Modern. */
struct ModernGrundfarbe
{
    const char *name;    ///< Name in ASCII, zugleich Schlüssel für den USB-Befehl \@modernfarbe und das Webportal
    uint32_t vorgabe;    ///< Vorgabe als 0xRRGGBB
    ModernGruppe gruppe; ///< Gruppe im Webportal
};

/** @brief Grundpalette von Modern in der Reihenfolge von ModernFarbe: neutrale schwarze Grundflächen, bläulich graphitfarbene Ebenen und kräftige Akzente. Die Vorgaben sind die Entwurfsfarben. */
inline constexpr ModernGrundfarbe ModernGrundfarben[ModernFarbenAnzahl] = {
    {"Grund", 0x000000, ModernGruppe::Fest},          // gleich Retro, weil Sperrbildschirmlogo, „made in germany“ und Stummsymbol Schwarz als Grund tragen
    {"Flaeche", 0x101010, ModernGruppe::Fest},        // gleich Retro, weil logo_135, Kassettenecken und Coverränder 0x1082 als Grund tragen
    {"Cyan", 0x00D2FF, ModernGruppe::Akzente},        // Titel, Rahmen, Marker, Regler, Pegel normal
    {"Blau", 0x4C8DFF, ModernGruppe::Akzente},        // IP, MP3, aktive Umschalttaste
    {"Violett", 0xA77BFF, ModernGruppe::Akzente},     // Kategorie System, Teiler, Timerbogen
    {"Magenta", 0xFF2D95, ModernGruppe::Akzente},     // Sekundenzeiger, Favoriten, Position, gewählter Titel
    {"Rot", 0xFF453A, ModernGruppe::Akzente},         // Warnung, Stopp, Nadel, Übersteuerung
    {"Orange", 0xFF8C2E, ModernGruppe::Akzente},      // Pegel hoch, Löschtaste, Sommerzeit
    {"Gelb", 0xFFD23F, ModernGruppe::Akzente},        // Hinweis, Stern, Bitrate, Zweitzeichen
    {"Gruen", 0x3CF07C, ModernGruppe::Akzente},       // Bestätigung, Wiedergabe, Schalter an, starkes Netz
    {"Text", 0xE2E7F2, ModernGruppe::Text},           // Haupttext
    {"Silber", 0xB4BED2, ModernGruppe::Text},         // Zweitschrift, Beschriftungen, Skalen
    {"Gedaempft", 0x737E96, ModernGruppe::Text},      // gedämpfter Text, inaktive Zustände
    {"Ebene1", 0x1A1E26, ModernGruppe::Erweitert},    // erhöhte Fläche: Tasten, Zeilen, Felder, Spuren
    {"Ebene2", 0x2A3140, ModernGruppe::Erweitert},    // zweite Ebene: Tastaturtasten, gewählte Zeile, Trenner
    {"Linie", 0x414A5C, ModernGruppe::Erweitert},     // Rahmen, Linien, Nebenmarken, zurückgenommene Einträge
    {"Weiss", 0xFFFFFF, ModernGruppe::Erweitert},     // hervorgehobener Text, Zeiger
    {"CyanTief", 0x0F4E63, ModernGruppe::Erweitert},  // Auswahlfläche, gedrückte Taste, Fortschritt
    {"ViolettTief", 0x2E1F5C, ModernGruppe::Erweitert}, // Sondertasten, Kopf System der Infoseite
    {"MagentaTief", 0x521035, ModernGruppe::Erweitert}, // Löschtaste des Zahlenblocks
};

/** @brief Grundfarben von Modern als 0xRRGGBB, Index nach ModernFarbe. */
struct ModernFarben
{
    uint32_t rgb[ModernFarbenAnzahl]; ///< 0xRRGGBB je Grundfarbe
};

/** @brief Liefert die Vorgaben aller Grundfarben aus ModernGrundfarben. */
constexpr ModernFarben modernVorgaben()
{
    ModernFarben farben{};
    for (size_t i = 0; i < ModernFarbenAnzahl; ++i)
        farben.rgb[i] = ModernGrundfarben[i].vorgabe;
    return farben;
}

inline constexpr ModernFarben ModernVorgabe = modernVorgaben(); ///< Vorgaben der Grundfarben von Modern

/** @brief Wandelt 0xRRGGBB in RGB565 um, abgeschnitten wie color565 von M5GFX. Bits über 23 zählen nicht. */
constexpr uint16_t rgb565(uint32_t rgb)
{
    return static_cast<uint16_t>(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

/** @brief Sucht eine Grundfarbe von Modern nach ihrem Namen aus ModernGrundfarben, Groß- und Kleinschreibung zählt. @return Index nach ModernFarbe oder ModernFarbenAnzahl bei unbekanntem Namen. */
constexpr size_t modernFarbeNachName(const char *name)
{
    for (size_t i = 0; name && i < ModernFarbenAnzahl; ++i)
    {
        size_t at = 0;
        while (name[at] && name[at] == ModernGrundfarben[i].name[at])
            ++at;
        if (name[at] == ModernGrundfarben[i].name[at])
            return i;
    }
    return ModernFarbenAnzahl;
}

/** @brief Liest genau sechs Hexziffern (Groß- oder Kleinbuchstaben) ohne Präfix und ohne Zeichen danach als 0xRRGGBB. @param text Nullterminierter Text. @param rgb Ziel, nur bei Erfolg beschrieben. @return true bei gültigem Wert. */
constexpr bool rgbAusHex(const char *text, uint32_t &rgb)
{
    if (!text)
        return false;
    uint32_t wert = 0;
    for (size_t i = 0; i < 6; ++i)
    {
        const char c = text[i];
        const int ziffer = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                        : c >= 'a' && c <= 'f'   ? c - 'a' + 10
                                                                                 : -1;
        if (ziffer < 0)
            return false;
        wert = (wert << 4) | static_cast<uint32_t>(ziffer);
    }
    if (text[6])
        return false;
    rgb = wert;
    return true;
}

constexpr uint32_t KeinEinzelwert = 0x10000; ///< Rückgabe einer Wertfunktion für berechnete und Tabellenrollen, außerhalb von RGB565

/**
 * @brief Wert einer Rolle in Modern: jede Rolle erbt die Farbe ihrer Familie aus den Grundfarben. Grund und Fläche bleiben bei ihrer Vorgabe. Nabe, Transparenzschlüssel und die Nadel des grünen VU-Blatts behalten den Retro-Wert, weil Kassetten und Blatt unverändert bleiben.
 * @param farben Grundfarben als 0xRRGGBB, Grund und Fläche darin zählen nicht.
 * @return RGB565 oder KeinEinzelwert.
 */
constexpr uint32_t modernWert(Rolle rolle, const ModernFarben &farben)
{
    const auto grundfarbe = [&farben](ModernFarbe farbe) -> uint32_t
    {
        const size_t i = static_cast<size_t>(farbe);
        return rgb565(ModernGrundfarben[i].gruppe == ModernGruppe::Fest ? ModernGrundfarben[i].vorgabe : farben.rgb[i]);
    };
    switch (rolle)
    {
    // Grund, Achse, Lücken und Schrift auf bunten Marken
    case Rolle::GemGrund:
    case Rolle::MainUhrAchse:
    case Rolle::SDBadgeText:
    case Rolle::DlgFavoritTasteRahmen:
    case Rolle::VisSegmentLuecke:
        return grundfarbe(ModernFarbe::Grund);
    case Rolle::GemFlaeche:
        return grundfarbe(ModernFarbe::Flaeche);
    // Erhöhte Flächen: Tasten, Felder, Zeilen, Spuren, Pegelgrund, Raster, Geisterziffern
    case Rolle::GemTastenFlaeche:
    case Rolle::GemTasteAus:
    case Rolle::GemScrollSpur:
    case Rolle::UhrDigitalSpur:
    case Rolle::MainTasteFlaeche:
    case Rolle::SDFortschrittGrund:
    case Rolle::SDDetailGrund:
    case Rolle::EinTitelFeld:
    case Rolle::EinZeile:
    case Rolle::EinPunkteAus:
    case Rolle::DlgEingabeFeld:
    case Rolle::DlgListenTrenner:
    case Rolle::DlgFortschrittGrund:
    case Rolle::VisPegelGrund:
    case Rolle::VisRaster:
        return grundfarbe(ModernFarbe::Ebene1);
    case Rolle::GemAugenRing:
    case Rolle::EinTrenner:
    case Rolle::EinZeileGewaehlt:
    case Rolle::EinNumTaste:
    case Rolle::DlgTaste:
    case Rolle::DlgFortschrittKappe:
        return grundfarbe(ModernFarbe::Ebene2);
    // Rahmen, Linien, Nebenmarken
    case Rolle::GemRahmen:
    case Rolle::GemTasteAusText:
    case Rolle::UhrAnalogMarken:
    case Rolle::MainUhrMarkenKlein:
    case Rolle::MainStatusAus:
    case Rolle::MainTasteRahmen:
    case Rolle::SperreTasteRahmen:
    case Rolle::SDTitelGespielt:
    case Rolle::EinPunktLeer:
    case Rolle::EinNumGedrueckt:
    case Rolle::VisSkalaKlein:
        return grundfarbe(ModernFarbe::Linie);
    // Text in vier Stufen
    case Rolle::GemTextGedaempft:
    case Rolle::GemTextSchwach:
    case Rolle::GemScrollDaumen:
    case Rolle::GemReglerNull:
    case Rolle::RadStereoAus:
    case Rolle::SDOrdnerLeer:
    case Rolle::EinStatusInaktiv:
    case Rolle::EinStatusLeer:
    case Rolle::EinSchalterGehaeuseAus:
    case Rolle::DlgListeScrollDaumen:
    case Rolle::VisSkalaLinie:
        return grundfarbe(ModernFarbe::Gedaempft);
    case Rolle::GemTextSilber:
    case Rolle::GemDialogText:
    case Rolle::GemSymbolAus:
    case Rolle::GemStummSymbol:
    case Rolle::MainStatusText:
    case Rolle::MainSsid:
    case Rolle::EinTextNeben:
    case Rolle::EinTastenText:
    case Rolle::VisSpitzeSilber:
    case Rolle::VisSkalaGross:
        return grundfarbe(ModernFarbe::Silber);
    case Rolle::GemText:
    case Rolle::UhrAnalogZeiger:
    case Rolle::MainDatum:
    case Rolle::SperreTasteText:
    case Rolle::EinText:
    case Rolle::EinSektionKopfText:
    case Rolle::VisGonioInfoText:
        return grundfarbe(ModernFarbe::Text);
    case Rolle::GemTextHell:
    case Rolle::MainUhrZeiger:
    case Rolle::DlgFavoritZahlText:
    case Rolle::VisSpitze:
        return grundfarbe(ModernFarbe::Weiss);
    // Hauptakzent: Titel, Dialograhmen, Marker, Regler, Digitalziffern, Pegel normal
    case Rolle::GemTitel:
    case Rolle::GemDialogRahmen:
    case Rolle::GemAkzent:
    case Rolle::GemScrollDaumenGezogen:
    case Rolle::GemReglerAktiv:
    case Rolle::UhrDigitalZiffern:
    case Rolle::MainUhrMarken:
    case Rolle::MainStatusWlan:
    case Rolle::MainTasteText:
    case Rolle::MainSymbolRadio:
    case Rolle::RadSenderMarke:
    case Rolle::EinSektionVerbindung:
    case Rolle::EinPanelTitel:
    case Rolle::EinPunkteAn:
    case Rolle::DlgCursor:
    case Rolle::DlgFortschrittBalken:
    case Rolle::DlgExplorerOrdner:
    case Rolle::VisPegelGruen:
    case Rolle::VisPpmGruen:
    case Rolle::VisPpmGelb:
    case Rolle::VisWelleLinks:
    case Rolle::VisGonioPunkt:
        return grundfarbe(ModernFarbe::Cyan);
    case Rolle::MainIp:
    case Rolle::SDBadgeMp3:
    case Rolle::DlgTasteAktiv:
        return grundfarbe(ModernFarbe::Blau);
    case Rolle::GemTeiler:
    case Rolle::UhrTimerBogen:
    case Rolle::SDBadgeAac:
    case Rolle::EinSektionSystem:
    case Rolle::VisGonioInfoMarke:
        return grundfarbe(ModernFarbe::Violett);
    // Gegenakzent: Sekunde, Favoriten, Position, gewählter Titel, rechter Kanal
    case Rolle::GemDialogTrenner:
    case Rolle::UhrTimerZahl:
    case Rolle::UhrSekunde:
    case Rolle::MainUhrSekunde:
    case Rolle::MainUhrAchseAkzent:
    case Rolle::MainSymbolSd:
    case Rolle::RadFavorit:
    case Rolle::SDTitelAktiv:
    case Rolle::SDFortschrittLinie:
    case Rolle::SDBadgeOgg:
    case Rolle::SDCoverInitialen:
    case Rolle::DlgFavoritZahlFlaeche:
    case Rolle::VisWelleRechts:
        return grundfarbe(ModernFarbe::Magenta);
    // Signalfarben
    case Rolle::GemWarnung:
    case Rolle::GemHausSymbol:
    case Rolle::RadNadel:
    case Rolle::SperrePulsFehler:
    case Rolle::EinSignalSchwach:
    case Rolle::EinStopSymbol:
    case Rolle::SDPlayWiederholListe:
    case Rolle::DlgSchliessen:
    case Rolle::DlgNetzSchwach:
    case Rolle::VisNadelKlassisch:
    case Rolle::VisPegelRot:
    case Rolle::VisGonioInfoPeak:
        return grundfarbe(ModernFarbe::Rot);
    case Rolle::MainStatusDst:
    case Rolle::SperreTasteLoeschen:
    case Rolle::SperrePulsLoeschen:
    case Rolle::VisPegelOrange:
        return grundfarbe(ModernFarbe::Orange);
    case Rolle::GemHinweis:
    case Rolle::MainSymbolKonfig:
    case Rolle::RadStern:
    case Rolle::SDBadgeBitrate:
    case Rolle::SDCoverText:
    case Rolle::SDPlayWiederholTitel:
    case Rolle::EinSignalMittel:
    case Rolle::DlgTasteZweitzeichen:
    case Rolle::DlgNetzMittel:
    case Rolle::DlgExplorerFlac:
    case Rolle::VisKorrelationMitte:
    case Rolle::VisGonioInfoTitel:
        return grundfarbe(ModernFarbe::Gelb);
    case Rolle::MainStatusNtp:
    case Rolle::SperreTasteOk:
    case Rolle::RadBitrate:
    case Rolle::SDBadgeFlac:
    case Rolle::SDPlayLaeuft:
    case Rolle::EinSchalterAn:
    case Rolle::EinPlaySymbol:
    case Rolle::DlgNetzStark:
    case Rolle::DlgExplorerMp3:
    case Rolle::VisGonioKorrelation:
        return grundfarbe(ModernFarbe::Gruen);
    // Tiefe Akzentflächen
    case Rolle::GemAuswahl:
    case Rolle::SperreTasteGedrueckt:
    case Rolle::SDFortschrittBalken:
    case Rolle::EinInfoKopfNetz:
    case Rolle::EinNumZurueck:
        return grundfarbe(ModernFarbe::CyanTief);
    case Rolle::EinInfoKopfSystem:
    case Rolle::DlgTasteSonder:
        return grundfarbe(ModernFarbe::ViolettTief);
    case Rolle::EinNumLoeschen:
        return grundfarbe(ModernFarbe::MagentaTief);
    // An Kassetten und das grüne VU-Blatt gebunden
    case Rolle::SDNabeRing:
    case Rolle::SDNabeDunkel:
    case Rolle::SDNabeZahnkranz:
    case Rolle::SDNabeAchse:
    case Rolle::SDTransparenz:
    case Rolle::VisNadelGruen:
        return ThemaRetro.farbe[static_cast<size_t>(rolle)];
    // Berechnete und Tabellenrollen
    case Rolle::SDPausePuls:
    case Rolle::EinSektionAbgeleitet:
    case Rolle::SDKassetteEtikettText:
    case Rolle::SDKassetteBand:
    case Rolle::SDKassetteBandKante:
    case Rolle::VisRegenbogen:
    case Rolle::VisWasserfallSkala:
    case Rolle::VisAuroraKalt:
    case Rolle::VisAuroraWarm:
    case Rolle::AugPhosphor:
    case Rolle::Anzahl:
        return KeinEinzelwert;
    }
    return KeinEinzelwert;
}

/** @brief Prüft, dass eine Wertfunktion jeder Rolle mit Einzelwert einen RGB565-Wert und jeder anderen KeinEinzelwert gibt. */
constexpr bool vollstaendig(uint32_t (*wert)(Rolle))
{
    for (size_t r = 0; r < RollenAnzahl; ++r)
        if ((wert(static_cast<Rolle>(r)) <= 0xFFFF) != (rollenArt(static_cast<Rolle>(r)) == RollenArt::Farbe))
            return false;
    return true;
}

/** @brief Wert einer Rolle in Modern mit den Vorgaben der Grundfarben. */
constexpr uint32_t modernVorgabeWert(Rolle rolle)
{
    return modernWert(rolle, ModernVorgabe);
}

// Welche Rollen einen Einzelwert bekommen, hängt nur vom switch in modernWert ab, nicht von den Grundfarben.
static_assert(vollstaendig(modernVorgabeWert), "Modern muss jeder Rolle mit Einzelwert eine Farbe geben");

/** @brief Baut ein Thema aus einer Wertfunktion je Rolle, Rollen mit KeinEinzelwert bleiben 0. */
constexpr Thema baueThema(const char *name, uint32_t (*wert)(Rolle), const Farbtabellen *tabellen, Bildsatz bildsatz, uint8_t merkmale)
{
    Thema thema{name, {}, tabellen, bildsatz, merkmale};
    for (size_t r = 0; r < RollenAnzahl; ++r)
        if (wert(static_cast<Rolle>(r)) <= 0xFFFF)
            thema.farbe[r] = static_cast<uint16_t>(wert(static_cast<Rolle>(r)));
    return thema;
}

/** @brief Baut Modern aus Grundfarben. Tabellenrollen und Bilder wie Retro, Rollen mit KeinEinzelwert bleiben 0. @param farben Grundfarben als 0xRRGGBB. Grund und Fläche darin zählen nicht. */
constexpr Thema baueModern(const ModernFarben &farben)
{
    Thema thema{"Individuell", {}, &RetroTabellen, Bildsatz::Retro, 0};
    for (size_t r = 0; r < RollenAnzahl; ++r)
    {
        const uint32_t wert = modernWert(static_cast<Rolle>(r), farben);
        if (wert <= 0xFFFF)
            thema.farbe[r] = static_cast<uint16_t>(wert);
    }
    return thema;
}

/** @brief Thema Modern mit den Vorgaben, zur Übersetzungszeit gebaut. Startwert des Themas Modern bis setzeModernFarben(). */
inline constexpr Thema ThemaModernVorgabe = baueModern(ModernVorgabe);

/** @brief Helligkeit eines RGB565-Werts 0..255: Kanäle wie getIntensityFrom565 auf 8 Bit erweitert, Gewichte nach BT.601. */
constexpr uint32_t helligkeit565(uint16_t wert)
{
    const uint32_t r = (((wert >> 11) & 31) * 527 + 23) >> 6, g = (((wert >> 5) & 63) * 259 + 33) >> 6, b = ((wert & 31) * 527 + 23) >> 6;
    return (77 * r + 150 * g + 29 * b) >> 8;
}

/** @brief Mischt zwei RGB565-Werte je Kanal in 8 Bit und rundet. anteil 0 ergibt genau von, 255 genau nach. */
constexpr uint16_t mische565(uint16_t von, uint16_t nach, uint32_t anteil)
{
    const int32_t a = static_cast<int32_t>(anteil > 255 ? 255 : anteil);
    const int32_t r0 = (((von >> 11) & 31) * 527 + 23) >> 6, g0 = (((von >> 5) & 63) * 259 + 33) >> 6, b0 = ((von & 31) * 527 + 23) >> 6;
    const int32_t r1 = (((nach >> 11) & 31) * 527 + 23) >> 6, g1 = (((nach >> 5) & 63) * 259 + 33) >> 6, b1 = ((nach & 31) * 527 + 23) >> 6;
    const int32_t r = r0 + ((r1 - r0) * a + (r1 >= r0 ? 127 : -127)) / 255;
    const int32_t g = g0 + ((g1 - g0) * a + (g1 >= g0 ? 127 : -127)) / 255;
    const int32_t b = b0 + ((b1 - b0) * a + (b1 >= b0 ? 127 : -127)) / 255;
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/**
 * @brief Grauton eines Bildpixels für Themen mit MerkmalGraustufen, nach seiner Helligkeit zwischen Tinte und Papier gemischt.
 * @param wert RGB565 des Originalbilds
 * @param tinte dunkelster Ton
 * @param papier hellster Ton
 * @param umkehren false für Abbildungen (Kassetten, Cover): Schwarz wird tinte, Weiß papier. true für helle Zeichnungen auf dunklem Grund (Logos, Platzhalter-Cover): grund und alles Dunklere wird papier, Weiß wird tinte.
 * @param grund eingebrannter Grund des Originalbilds, nur bei umkehren
 * @return RGB565
 */
constexpr uint16_t bildGrau(uint16_t wert, uint16_t tinte, uint16_t papier, bool umkehren = false, uint16_t grund = 0)
{
    const uint32_t hell = helligkeit565(wert);
    if (!umkehren)
        return mische565(tinte, papier, hell);
    const uint32_t boden = helligkeit565(grund);
    return mische565(papier, tinte, hell <= boden ? 0 : (hell - boden) * 255 / (255 - boden));
}

/** @brief Grundpalette von E-Paper in RGB565: Altweiß und warme Grautöne von Papier bis Tinte, dazu ein gedecktes Rot als einzige Akzentfarbe. */
namespace EPaperPalette
{
    constexpr uint16_t Papier = 0xEF5B;    ///< #ECE8DD Altweiß: Grund, Tastenfüllung, Schrift auf dunklen Marken
    constexpr uint16_t Flaeche = 0xE6FA;   ///< #E3DFD3 Arbeitsfläche: Skala, Panel, Playlist, Fußzeilen
    constexpr uint16_t Ebene1 = 0xD698;    ///< #D6D1C5 erhöhte Fläche: Zeilen, Felder, Spuren, Geisterziffern
    constexpr uint16_t Ebene2 = 0xC616;    ///< #C6C1B4 zweite Ebene: Tastaturtasten, gewählte Zeile, Trenner
    constexpr uint16_t Auswahl = 0xAD53;   ///< #ADA89B Auswahl und aktive Tasten unter dunkler Schrift
    constexpr uint16_t Linie = 0x9CB1;     ///< #9A958A Rahmen, Linien, Nebenmarken
    constexpr uint16_t Gedaempft = 0x738D; ///< #77736A gedämpfter Text, inaktive Zustände, Teiler
    constexpr uint16_t Grafit = 0x5289;    ///< #57534D Zweitschrift, Marken, Kategorie System
    constexpr uint16_t Text = 0x3186;      ///< #353330 Haupttext
    constexpr uint16_t Tinte = 0x18C3;     ///< #1C1B19 hervorgehobener Text, Zeiger, Titel, Symbole, aktive Zustände
    constexpr uint16_t Rot = 0xA9C5;       ///< #A8382E gedecktes Rot, nur für die acht Rollen mit E-Paper-Akzent
}

/** @brief Wert einer Rolle in E-Paper: Rot nur für die acht Rollen mit E-Paper-Akzent, alle anderen erben einen Grauton ihrer Familie aus EPaperPalette. Die Nabenfarben der Kassetten sind wie die Kassettenbilder in Grau umgerechnet. @return RGB565 oder KeinEinzelwert. */
constexpr uint32_t ePaperWert(Rolle rolle)
{
    using namespace EPaperPalette;
    switch (rolle)
    {
    // Akzent: Sekundenzeiger und Uhrmitte, Abstimmnadel, VU-Nadeln, Übersteuerung, Stopp und Warnung
    case Rolle::GemWarnung:
    case Rolle::UhrSekunde:
    case Rolle::MainUhrSekunde:
    case Rolle::MainUhrAchseAkzent:
    case Rolle::RadNadel:
    case Rolle::VisNadelKlassisch:
    case Rolle::VisNadelGruen:
    case Rolle::VisPegelRot:
        return Rot;
    // Papier: Grund, Tastenfüllung, Zeigerachse, Segmentlücken und Schrift auf dunklen Marken
    case Rolle::GemGrund:
    case Rolle::GemTastenFlaeche:
    case Rolle::MainUhrAchse:
    case Rolle::MainTasteFlaeche:
    case Rolle::SDBadgeText:
    case Rolle::DlgFavoritTasteRahmen:
    case Rolle::DlgFavoritZahlText:
    case Rolle::VisSegmentLuecke:
        return Papier;
    case Rolle::GemFlaeche:
        return Flaeche;
    // Erhöhte Flächen: Zeilen, Felder, Spuren, Pegelgrund, Raster, Geisterziffern
    case Rolle::GemTasteAus:
    case Rolle::GemScrollSpur:
    case Rolle::UhrDigitalSpur:
    case Rolle::SDFortschrittGrund:
    case Rolle::SDDetailGrund:
    case Rolle::EinTitelFeld:
    case Rolle::EinZeile:
    case Rolle::EinPunkteAus:
    case Rolle::DlgEingabeFeld:
    case Rolle::DlgListenTrenner:
    case Rolle::DlgFortschrittGrund:
    case Rolle::VisPegelGrund:
    case Rolle::VisRaster:
        return Ebene1;
    case Rolle::GemAugenRing:
    case Rolle::EinTrenner:
    case Rolle::EinZeileGewaehlt:
    case Rolle::EinNumTaste:
    case Rolle::DlgTaste:
    case Rolle::DlgFortschrittKappe:
        return Ebene2;
    // Auswahl und aktive Flächen, darauf Schrift in Tinte
    case Rolle::GemAuswahl:
    case Rolle::SperreTasteGedrueckt:
    case Rolle::SDFortschrittBalken:
    case Rolle::EinInfoKopfNetz:
    case Rolle::EinInfoKopfSystem:
    case Rolle::EinNumZurueck:
    case Rolle::EinNumLoeschen:
    case Rolle::DlgTasteAktiv:
    case Rolle::DlgTasteSonder:
    case Rolle::DlgSchliessen:
        return Auswahl;
    // Rahmen, Linien, Nebenmarken
    case Rolle::GemRahmen:
    case Rolle::GemTasteAusText:
    case Rolle::GemSymbolAus:
    case Rolle::UhrAnalogMarken:
    case Rolle::MainUhrMarkenKlein:
    case Rolle::MainStatusAus:
    case Rolle::MainTasteRahmen:
    case Rolle::SperreTasteRahmen:
    case Rolle::SDTitelGespielt:
    case Rolle::EinPunktLeer:
    case Rolle::EinNumGedrueckt:
    case Rolle::VisSkalaKlein:
        return Linie;
    // Schrift in vier Stufen
    case Rolle::GemTextGedaempft:
    case Rolle::GemTextSchwach:
    case Rolle::GemScrollDaumen:
    case Rolle::GemTeiler:
    case Rolle::GemReglerNull:
    case Rolle::RadStereoAus:
    case Rolle::SDOrdnerLeer:
    case Rolle::SDPlayWiederholListe:
    case Rolle::EinStatusInaktiv:
    case Rolle::EinStatusLeer:
    case Rolle::EinSchalterGehaeuseAus:
    case Rolle::EinSignalSchwach:
    case Rolle::DlgTasteZweitzeichen:
    case Rolle::DlgNetzSchwach:
    case Rolle::DlgListeScrollDaumen:
    case Rolle::VisSkalaLinie:
    case Rolle::VisWelleRechts:
        return Gedaempft;
    case Rolle::GemTextSilber:
    case Rolle::GemDialogText:
    case Rolle::GemDialogTrenner:
    case Rolle::GemHinweis:
    case Rolle::GemStummSymbol:
    case Rolle::UhrTimerBogen:
    case Rolle::MainStatusText:
    case Rolle::MainSsid:
    case Rolle::MainIp:
    case Rolle::RadSenderMarke:
    case Rolle::RadBitrate:
    case Rolle::SDBadgeMp3:
    case Rolle::SDBadgeFlac:
    case Rolle::SDBadgeOgg:
    case Rolle::SDBadgeAac:
    case Rolle::SDBadgeBitrate:
    case Rolle::SDCoverInitialen:
    case Rolle::SDPlayWiederholTitel:
    case Rolle::EinTextNeben:
    case Rolle::EinTastenText:
    case Rolle::EinSektionSystem:
    case Rolle::EinSignalMittel:
    case Rolle::DlgNetzMittel:
    case Rolle::DlgFavoritZahlFlaeche:
    case Rolle::DlgExplorerMp3:
    case Rolle::DlgExplorerFlac:
    case Rolle::VisPegelOrange:
    case Rolle::VisKorrelationMitte:
    case Rolle::VisSpitzeSilber:
    case Rolle::VisPpmGelb:
    case Rolle::VisSkalaGross:
    case Rolle::VisGonioKorrelation:
    case Rolle::VisGonioInfoMarke:
    case Rolle::VisGonioInfoTitel:
        return Grafit;
    case Rolle::GemText:
    case Rolle::UhrTimerZahl:
    case Rolle::UhrAnalogZeiger:
    case Rolle::MainDatum:
    case Rolle::SperreTasteText:
    case Rolle::SperreTasteLoeschen:
    case Rolle::SDCoverText:
    case Rolle::EinText:
    case Rolle::VisGonioInfoText:
    case Rolle::VisGonioInfoPeak:
        return Text;
    // Tinte: hervorgehobener Text, Zeiger, Titel, Dialograhmen, Symbole, Marker, aktive Zustände und Pegel
    case Rolle::GemTextHell:
    case Rolle::GemTitel:
    case Rolle::GemDialogRahmen:
    case Rolle::GemAkzent:
    case Rolle::GemScrollDaumenGezogen:
    case Rolle::GemReglerAktiv:
    case Rolle::UhrDigitalZiffern:
    case Rolle::MainUhrMarken:
    case Rolle::MainUhrZeiger:
    case Rolle::MainStatusNtp:
    case Rolle::MainStatusDst:
    case Rolle::MainStatusWlan:
    case Rolle::MainTasteText:
    case Rolle::MainSymbolRadio:
    case Rolle::MainSymbolSd:
    case Rolle::MainSymbolKonfig:
    case Rolle::GemHausSymbol:
    case Rolle::SperrePulsLoeschen:
    case Rolle::SperreTasteOk:
    case Rolle::SperrePulsFehler:
    case Rolle::RadFavorit:
    case Rolle::RadStern:
    case Rolle::SDTitelAktiv:
    case Rolle::SDFortschrittLinie:
    case Rolle::SDPlayLaeuft:
    case Rolle::EinSektionKopfText:
    case Rolle::EinSektionVerbindung:
    case Rolle::EinSchalterAn:
    case Rolle::EinPanelTitel:
    case Rolle::EinPunkteAn:
    case Rolle::EinStopSymbol:
    case Rolle::EinPlaySymbol:
    case Rolle::DlgCursor:
    case Rolle::DlgNetzStark:
    case Rolle::DlgFortschrittBalken:
    case Rolle::DlgExplorerOrdner:
    case Rolle::VisPegelGruen:
    case Rolle::VisSpitze:
    case Rolle::VisPpmGruen:
    case Rolle::VisWelleLinks:
    case Rolle::VisGonioPunkt:
        return Tinte;
    // An die Kassetten gebunden: Grau wie die umgerechneten Kassettenbilder
    case Rolle::SDNabeRing:
    case Rolle::SDNabeDunkel:
    case Rolle::SDNabeZahnkranz:
    case Rolle::SDNabeAchse:
    case Rolle::SDTransparenz:
        return bildGrau(ThemaRetro.farbe[static_cast<size_t>(rolle)], Tinte, Papier);
    // Berechnete und Tabellenrollen
    case Rolle::SDPausePuls:
    case Rolle::EinSektionAbgeleitet:
    case Rolle::SDKassetteEtikettText:
    case Rolle::SDKassetteBand:
    case Rolle::SDKassetteBandKante:
    case Rolle::VisRegenbogen:
    case Rolle::VisWasserfallSkala:
    case Rolle::VisAuroraKalt:
    case Rolle::VisAuroraWarm:
    case Rolle::AugPhosphor:
    case Rolle::Anzahl:
        return KeinEinzelwert;
    }
    return KeinEinzelwert;
}

static_assert(vollstaendig(ePaperWert), "E-Paper muss jeder Rolle mit Einzelwert eine Farbe geben");

/**
 * @brief Reihen der Tabellenrollen von E-Paper, alle in Grau.
 *
 * Kassettenreihen wie die Kassettenbilder umgerechnet. Der Regenbogen läuft zyklisch von Tinte bis Gedaempft und zurück, damit jede Stufe auf Papier lesbar bleibt. Der Wasserfall steigt von Papier (Stille) bis Tinte. Nordlicht und Auge werden mit MerkmalHellerGrund vom Grund abgezogen, ihre Werte sind deshalb Tintendichten: gleiche Kanalanteile ergeben Grau, die warme Fassung des Nordlichts ist dichter.
 */
constexpr Farbtabellen ePaperTabellen()
{
    using namespace EPaperPalette;
    Farbtabellen tabellen = RetroTabellen;
    for (int i = 0; i < 8; ++i)
    {
        tabellen.kassetteEtikettText[i] = bildGrau(RetroTabellen.kassetteEtikettText[i], Tinte, Papier);
        tabellen.kassetteBand[i] = bildGrau(RetroTabellen.kassetteBand[i], Tinte, Papier);
        tabellen.kassetteBandKante[i] = bildGrau(RetroTabellen.kassetteBandKante[i], Tinte, Papier);
    }
    constexpr int stufen = sizeof(tabellen.regenbogen) / sizeof(tabellen.regenbogen[0]);
    for (int i = 0; i < stufen; ++i)
        tabellen.regenbogen[i] = mische565(Tinte, Gedaempft, static_cast<uint32_t>((i < stufen / 2 ? i : stufen - i) * 255 / (stufen / 2)));
    const uint16_t wasserfall[] = {Papier, Ebene2, Linie, Gedaempft, Grafit, Tinte};
    for (int i = 0; i < 6; ++i)
        tabellen.wasserfall[i] = wasserfall[i];
    const uint8_t kalt[3][3] = {{150, 150, 150}, {110, 110, 110}, {130, 130, 130}}, warm[3][3] = {{190, 190, 190}, {150, 150, 150}, {170, 170, 170}};
    for (int schleier = 0; schleier < 3; ++schleier)
        for (int kanal = 0; kanal < 3; ++kanal)
        {
            tabellen.auroraKalt[schleier][kanal] = kalt[schleier][kanal];
            tabellen.auroraWarm[schleier][kanal] = warm[schleier][kanal];
        }
    tabellen.augePhosphor = {185.0f, 30.0f, 120.0f, 60.0f, 40.0f, 70.0f, 220.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.88f, 0.95f, 0.72f, 50.0f, 110.0f, 1.0f, 1.0f};
    return tabellen;
}

/** @brief Reihen der Tabellenrollen von E-Paper. */
inline constexpr Farbtabellen EPaperTabellen = ePaperTabellen();

/** @brief Thema E-Paper, Index 2: hell, grau mit einem gedeckten Rot. Bilder werden beim Aufbau in Grau umgerechnet. */
inline constexpr Thema ThemaEPaper = baueThema("E-Paper", ePaperWert, &EPaperTabellen, Bildsatz::GrauLaufzeit, MerkmalGraustufen | MerkmalHellerGrund | MerkmalAkzentarm);

/** @brief Baut Modern aus gespeicherten Grundfarben neu. Einmal beim Start vor setzeThema() und vor jedem begin() oder prepare(), das Farben einbrennt. Ohne Aufruf trägt Modern die Vorgaben. @param farben Grundfarben als 0xRRGGBB. Grund und Fläche darin zählen nicht. */
void setzeModernFarben(const ModernFarben &farben);
/** @brief Wählt das aktive Thema. Einmal beim Start vor jedem begin() oder prepare(), das Farben einbrennt. @param index Gespeicherter Index. Unbekannte Indizes ergeben Retro. */
void setzeThema(uint8_t index);
/** @brief Liefert das aktive Thema, vor setzeThema() Retro. */
const Thema &themaAktiv();
/** @brief Liefert ein Thema nach gespeichertem Index, ohne das aktive zu wechseln. Für Vorschauen. @param index Gespeicherter Index, unbekannte Indizes ergeben Retro. */
const Thema &themaNachIndex(uint8_t index);

/** @brief RGB565-Wert einer Rolle im aktiven Thema, nur für RollenArt::Farbe aussagekräftig. */
inline uint16_t farbe(Rolle rolle)
{
    return themaAktiv().farbe[static_cast<size_t>(rolle)];
}

/** @brief RGB565-Wert einer Rolle in einem bestimmten Thema, etwa für die Vorschau eines nicht aktiven Themas. Nur für RollenArt::Farbe aussagekräftig. */
inline uint16_t farbe(const Thema &thema, Rolle rolle)
{
    return thema.farbe[static_cast<size_t>(rolle)];
}

/** @brief Reihen der Tabellenrollen im aktiven Thema. */
inline const Farbtabellen &farbtabellen()
{
    return *themaAktiv().tabellen;
}

#endif
