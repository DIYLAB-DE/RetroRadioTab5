#include "Strings_retroradio.h"
#include "I18n.h"

#include <pgmspace.h>

namespace
{
    // Deutsch

    // Dialog-Titel und -Texte
    const char DE_PRESET_DLG_TITLE[] PROGMEM = "FAVORITEN";
    const char DE_RESET_DLG_TITLE[] PROGMEM = "ZURÜCKSETZEN?";
    const char DE_RESET_DLG_MESSAGE[] PROGMEM = "Die Lautstärke, der Ton und die Visualisierungen werden zurückgesetzt.";
    const char DE_SLEEP_DLG_TITLE[] PROGMEM = "SLEEP-TIMER";
    const char DE_SLEEP_DLG_MESSAGE[] PROGMEM = "Radio verlassen nach:";

    // Control-Labels
    const char DE_KNOB_LABEL[] PROGMEM = "Lautstärke";
    const char DE_TRACK_BALANCE[] PROGMEM = "Balance";
    const char DE_TRACK_LOW[] PROGMEM = "Tiefen";
    const char DE_TRACK_MID[] PROGMEM = "Mitten";
    const char DE_TRACK_HIGH[] PROGMEM = "Höhen";

    // Visualizer
    const char DE_VIS_GAIN_LABEL[] PROGMEM = "Verstärkungsfaktor";
    const char DE_VIS_GONIO_LEFT[] PROGMEM = "links";
    const char DE_VIS_GONIO_RIGHT[] PROGMEM = "rechts";

    // Englisch (britisch)

    // Dialog-Titel und -Texte
    const char EN_PRESET_DLG_TITLE[] PROGMEM = "FAVOURITES";
    const char EN_RESET_DLG_TITLE[] PROGMEM = "RESET?";
    const char EN_RESET_DLG_MESSAGE[] PROGMEM = "Volume, sound and visualisations will be reset.";
    const char EN_SLEEP_DLG_TITLE[] PROGMEM = "SLEEP TIMER";
    const char EN_SLEEP_DLG_MESSAGE[] PROGMEM = "Quit radio after:";

    // Control-Labels
    const char EN_KNOB_LABEL[] PROGMEM = "Volume";
    const char EN_TRACK_BALANCE[] PROGMEM = "Balance";
    const char EN_TRACK_LOW[] PROGMEM = "Bass";
    const char EN_TRACK_MID[] PROGMEM = "Mid";
    const char EN_TRACK_HIGH[] PROGMEM = "Treble";

    // Visualizer
    const char EN_VIS_GAIN_LABEL[] PROGMEM = "Gain factor";
    const char EN_VIS_GONIO_LEFT[] PROGMEM = "left";
    const char EN_VIS_GONIO_RIGHT[] PROGMEM = "right";

    const char DE_RADIO_ERROR[] PROGMEM = "Stream unterbrochen";
    const char EN_RADIO_ERROR[] PROGMEM = "Connection interrupted. Start again.";

    /** @brief Textzeiger je Schlüssel und Sprache, die Reihenfolge muss der von RetroRadioKey entsprechen. */
    PGM_P const RETRORADIO_TABLE[static_cast<size_t>(RetroRadioKey::RETRORADIO_KEY_COUNT)]
                                [static_cast<size_t>(LangIndex::COUNT)] PROGMEM =
                                    {
                                        // Dialog-Texte
                                        {DE_PRESET_DLG_TITLE, EN_PRESET_DLG_TITLE},
                                        {DE_RESET_DLG_TITLE, EN_RESET_DLG_TITLE},
                                        {DE_RESET_DLG_MESSAGE, EN_RESET_DLG_MESSAGE},
                                        {DE_SLEEP_DLG_TITLE, EN_SLEEP_DLG_TITLE},
                                        {DE_SLEEP_DLG_MESSAGE, EN_SLEEP_DLG_MESSAGE},

                                        // Control-Labels
                                        {DE_KNOB_LABEL, EN_KNOB_LABEL},
                                        {DE_TRACK_BALANCE, EN_TRACK_BALANCE},
                                        {DE_TRACK_LOW, EN_TRACK_LOW},
                                        {DE_TRACK_MID, EN_TRACK_MID},
                                        {DE_TRACK_HIGH, EN_TRACK_HIGH},

                                        // Visualizer
                                        {DE_VIS_GAIN_LABEL, EN_VIS_GAIN_LABEL},
                                        {DE_VIS_GONIO_LEFT, EN_VIS_GONIO_LEFT},
                                        {DE_VIS_GONIO_RIGHT, EN_VIS_GONIO_RIGHT},
                                        {DE_RADIO_ERROR, EN_RADIO_ERROR}};

    static_assert(sizeof(RETRORADIO_TABLE) / sizeof(RETRORADIO_TABLE[0]) == static_cast<size_t>(RetroRadioKey::RETRORADIO_KEY_COUNT),
                  "RETRORADIO_TABLE / RetroRadioKey out of sync");

    const char EMPTY_STR[] PROGMEM = "";
}

const __FlashStringHelper *i18n_getRetroRadio(RetroRadioKey key, LangIndex lang)
{
    const auto idx = static_cast<size_t>(key);
    if (idx >= static_cast<size_t>(RetroRadioKey::RETRORADIO_KEY_COUNT))
        return reinterpret_cast<const __FlashStringHelper *>(EMPTY_STR);

    auto li = static_cast<size_t>(lang);
    if (li >= static_cast<size_t>(LangIndex::COUNT))
        li = 0;

    PGM_P p = reinterpret_cast<PGM_P>(pgm_read_ptr(&RETRORADIO_TABLE[idx][li]));
    if (!p && li != 0)
        p = reinterpret_cast<PGM_P>(pgm_read_ptr(&RETRORADIO_TABLE[idx][0]));
    if (!p)
        return reinterpret_cast<const __FlashStringHelper *>(EMPTY_STR);
    return reinterpret_cast<const __FlashStringHelper *>(p);
}
