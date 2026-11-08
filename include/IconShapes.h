#ifndef ICON_SHAPES_H
#define ICON_SHAPES_H

#include <cstdint>

// Selektiver Import aus DIYLAB-DE/RetroRadio, ea4229a34fb39faaff23619092cf09cafb8c51cf. GPL-3.0, siehe referenz/RetroRadio-LICENSE.
/** @brief Symbole für MainScreen, Stummschaltung und Dateiauswahl. */
enum class IconId : uint8_t
{
    Radio,    ///< MainScreen: Radio
    Player,   ///< MainScreen: SD-Player
    Config,   ///< MainScreen: Konfiguration
    Mute,     ///< Stummschaltung
    Folder,   ///< Dateiauswahl: Ordner
    FolderUp, ///< Dateiauswahl: übergeordneter Ordner
    Count     ///< Anzahl der Symbole, kein gültiges Symbol
};
/** @brief Grundformen der gespeicherten Symboldaten. */
enum class IconShapeKind : uint8_t
{
    Segment,       ///< Strecke mit runden Enden
    Polyline,      ///< Streckenzug
    Ring,          ///< Kreisring
    Arc,           ///< Kreisbogen
    FilledPolygon  ///< Gefülltes Vieleck mit Even-Odd-Regel
};

/** @brief Symbolgeometrie in den lokalen Koordinaten der jeweiligen Kachel. */
struct IconShape
{
    IconShapeKind kind;  ///< Art der Form
    float values[5];     ///< Formparameter, je nach kind
    uint16_t firstPoint; ///< Index des ersten Punkts in den Punktdaten
    uint8_t pointCount;  ///< Anzahl der Punkte
    bool closed;         ///< Streckenzug oder Kontur geschlossen
};

/** @brief Ein Symbol mit gemeinsamer Strichbreite und Formenliste. */
struct IconDefinition
{
    const IconShape *shapes; ///< Formenliste
    uint8_t shapeCount;      ///< Anzahl der Formen
    float stroke;            ///< Strichbreite aller Formen
};

namespace IconShapes
{
    constexpr int Size = 80; ///< Kantenlänge der Standardkachel in Pixeln

    /** @brief Breite der gespeicherten Symbolkachel in Pixeln. */
    constexpr int width(IconId id)
    {
        return id == IconId::Folder ? 19 : id == IconId::FolderUp ? 17
                                       : id == IconId::Mute       ? 28
                                                                  : 80;
    }

    /** @brief Höhe der gespeicherten Symbolkachel in Pixeln. */
    constexpr int height(IconId id)
    {
        return id == IconId::Folder || id == IconId::FolderUp ? 16 : id == IconId::Mute ? 28
                                                                                        : 80;
    }

    /** @brief Zeichnet die Formen eines Symbols einmalig in einen RGB565-Puffer. @param id Symbol. @param color Vordergrund. @param background Hintergrund. @param scale Native Neuberechnung mit Faktor 1 bis 2 (Ordner bis 6), auch gebrochen wie 1,5. Kantenlänge wird gerundet. @return Mit free() freizugebender PSRAM-Puffer oder nullptr bei ungültigen Eingaben bzw. Speicherfehler. */
    uint16_t *draw(IconId id, uint16_t color, uint16_t background, float scale = 1);
}

#endif
