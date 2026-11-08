#ifndef ICON_SHAPE_DATA_H
#define ICON_SHAPE_DATA_H

#include "IconShapes.h"

// Die drei MainScreen-Symbole wurden mit dem früheren Skript scripts/importiere_symbolformen.py (Stand Commit e873c9b) erzeugt, Stumm- und Ordnersymbole kamen später aus derselben Quelle hinzu.
// Quelle: DIYLAB-DE/RetroRadio, ea4229a34fb39faaff23619092cf09cafb8c51cf, include/IconShapeData.h.
// Radio und Konfiguration: Phosphor Icons thin (MIT). Player: Eigenmaterial des Projektinhabers.
// Unveränderte Koordinaten, Punkttabelle und Indizes auf die drei verwendeten Symbole reduziert.
/*
MIT License

Copyright (c) 2023 Phosphor Icons

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

namespace
{
constexpr float ICON_POINTS[] = {
    2.5f, 24.375f, 74.375f, 24.375f, 75.938f, 24.794f, 77.081f, 25.938f, 77.5f, 27.5f, 77.5f, 71.25f,
    77.5f, 71.25f, 77.081f, 72.812f, 75.938f, 73.956f, 74.375f, 74.375f, 5.625f, 74.375f, 4.062f, 73.956f,
    4.062f, 73.956f, 2.919f, 72.812f, 2.5f, 71.25f, 2.5f, 24.375f, 56.68f, 66.26f, 55.99f, 66.84f,
    55.99f, 69.15f, 56.45f, 69.73f, 57.49f, 69.27f, 57.49f, 66.84f, 57.14f, 66.38f, 23.2f, 66.26f,
    22.51f, 66.73f, 22.51f, 69.27f, 22.97f, 69.73f, 24.01f, 69.15f, 23.66f, 66.38f, 51.14f, 63.72f,
    50.68f, 64.3f, 50.68f, 69.15f, 51.26f, 69.73f, 52.18f, 69.27f, 52.18f, 64.07f, 51.83f, 63.72f,
    28.51f, 63.03f, 27.82f, 63.61f, 27.82f, 69.27f, 28.28f, 69.73f, 29.32f, 69.15f, 29.09f, 63.26f,
    45.6f, 60.84f, 45.14f, 61.3f, 45.14f, 69.27f, 45.6f, 69.73f, 46.64f, 69.15f, 46.64f, 61.41f,
    46.18f, 60.84f, 33.94f, 59.57f, 33.36f, 60.26f, 33.36f, 69.15f, 33.94f, 69.73f, 34.86f, 69.27f,
    34.86f, 60.03f, 34.4f, 59.57f, 39.83f, 57.26f, 39.25f, 57.95f, 39.25f, 69.15f, 39.71f, 69.73f,
    40.75f, 69.27f, 40.75f, 57.84f, 40.4f, 57.37f, 14.78f, 51.49f, 15.35f, 52.41f, 25.86f, 52.41f,
    26.44f, 53.68f, 28.4f, 54.95f, 31.05f, 54.49f, 32.55f, 52.41f, 64.65f, 52.41f, 65.22f, 51.72f,
    64.76f, 50.91f, 32.55f, 50.91f, 31.28f, 48.95f, 29.67f, 48.25f, 27.24f, 48.83f, 25.74f, 50.91f,
    15.24f, 50.91f, 28.17f, 49.99f, 29.55f, 49.76f, 30.82f, 50.68f, 31.05f, 51.95f, 30.71f, 52.76f,
    29.67f, 53.45f, 28.17f, 53.22f, 27.24f, 51.83f, 27.59f, 50.56f, 36.02f, 20.55f, 34.98f, 21.01f,
    34.29f, 22.16f, 34.29f, 35.21f, 35.21f, 36.59f, 36.94f, 36.83f, 47.56f, 30.13f, 48.14f, 28.05f,
    47.1f, 26.67f, 36.59f, 20.55f, 36.13f, 22.05f, 46.64f, 28.28f, 46.29f, 29.21f, 36.36f, 35.44f,
    35.79f, 35.09f, 35.79f, 22.39f, 38.56f, 12.58f, 34.17f, 13.62f, 29.55f, 16.39f, 26.2f, 20.43f,
    25.05f, 22.86f, 24.13f, 26.67f, 24.59f, 32.67f, 26.2f, 36.48f, 27.47f, 38.33f, 31.86f, 42.14f,
    35.56f, 43.75f, 38.56f, 44.33f, 44.56f, 43.75f, 48.02f, 42.25f, 50.33f, 40.63f, 54.03f, 36.13f,
    55.18f, 33.48f, 55.87f, 30.24f, 55.41f, 24.24f, 54.14f, 21.01f, 52.29f, 18.24f, 48.02f, 14.66f,
    44.56f, 13.16f, 41.56f, 12.58f, 38.67f, 14.08f, 43.98f, 14.54f, 49.06f, 17.2f, 52.64f, 21.47f,
    54.37f, 26.67f, 54.49f, 28.86f, 53.91f, 32.44f, 51.37f, 37.4f, 47.22f, 40.98f, 44.33f, 42.25f,
    41.56f, 42.83f, 36.13f, 42.37f, 31.28f, 39.94f, 27.47f, 35.67f, 25.63f, 30.24f, 25.97f, 24.93f,
    27.36f, 21.47f, 28.98f, 19.16f, 33.25f, 15.7f, 35.79f, 14.66f, 38.56f, 14.2f, 11.31f, 4.5f,
    9.23f, 6.81f, 8.2f, 9.58f, 8.2f, 70.42f, 8.66f, 72.15f, 10.04f, 74.34f, 12.0f, 75.96f,
    15.35f, 77.0f, 64.76f, 77.0f, 66.84f, 76.54f, 69.15f, 75.15f, 71.23f, 72.38f, 71.8f, 70.42f,
    71.8f, 9.7f, 71.11f, 7.39f, 69.84f, 5.54f, 67.07f, 3.58f, 64.53f, 3.0f, 15.35f, 3.0f,
    12.93f, 3.58f, 11.77f, 6.12f, 14.89f, 4.62f, 65.11f, 4.62f, 67.3f, 5.42f, 69.03f, 6.93f,
    70.19f, 9.23f, 70.42f, 11.66f, 70.42f, 68.34f, 69.84f, 71.8f, 68.0f, 74.11f, 65.22f, 75.38f,
    14.78f, 75.38f, 12.47f, 74.46f, 10.97f, 73.07f, 9.7f, 70.3f, 9.7f, 9.7f, 10.5f, 7.62f,
8.5f, 10.0f, 3.0f, 10.0f, 3.0f, 18.0f, 8.5f, 18.0f, 14.0f, 24.0f, 14.0f, 4.0f,
17.333f, 15.05f, 1.69f, 15.05f, 1.295f, 14.915f, 1.128f, 14.724f, 1.04f, 14.4f, 1.04f, 3.77f, 1.04f, 3.77f, 17.255f, 3.77f, 17.684f, 3.916f, 17.866f, 4.123f, 17.96f, 4.475f, 17.96f, 14.423f, 17.96f, 14.423f, 17.776f, 14.866f, 17.333f, 15.05f, 1.04f, 3.77f, 1.064f, 1.473f, 1.316f, 1.096f, 1.745f, 0.95f, 6.621f, 0.99f, 6.886f, 1.156f, 6.886f, 1.156f, 9.5f, 3.77f, 10.287f, 4.425f, 6.713f, 0.85f, 3.138f, 4.425f, 13.863f, 15.15f, 12.929f, 15.089f, 12.012f, 14.906f, 11.126f, 14.606f, 10.288f, 14.192f, 9.51f, 13.672f, 9.51f, 13.672f, 8.807f, 13.056f, 8.19f, 12.353f, 7.67f, 11.575f, 7.257f, 10.736f, 6.956f, 9.851f, 6.956f, 9.851f, 6.774f, 8.933f, 6.713f, 8.0f, 6.713f, 0.85f
};

constexpr IconShape ICON_SHAPES_RADIO[] = {
    {IconShapeKind::Polyline, {0, 0, 0, 0, 0}, 0, 6, false},
    {IconShapeKind::Polyline, {0, 0, 0, 0, 0}, 6, 6, false},
    {IconShapeKind::Polyline, {0, 0, 0, 0, 0}, 12, 4, false},
    {IconShapeKind::Segment, {2.5f, 24.375f, 65.0f, 5.625f, 0.0f}, 0, 0, false},
    {IconShapeKind::Ring, {52.5f, 49.375f, 12.5f, 0.0f, 0.0f}, 0, 0, false},
    {IconShapeKind::Segment, {15.0f, 36.875f, 27.5f, 36.875f, 0.0f}, 0, 0, false},
    {IconShapeKind::Segment, {15.0f, 49.375f, 27.5f, 49.375f, 0.0f}, 0, 0, false},
    {IconShapeKind::Segment, {15.0f, 61.875f, 27.5f, 61.875f, 0.0f}, 0, 0, false},
};

constexpr IconShape ICON_SHAPES_PLAYER[] = {
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 16, 7, true},
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 23, 6, true},
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 29, 7, true},
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 36, 6, true},
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 42, 7, true},
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 49, 7, true},
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 56, 7, true},
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 63, 16, true},
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 79, 9, true},
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 88, 10, true},
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 98, 6, true},
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 104, 24, true},
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 128, 21, true},
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 149, 20, true},
    {IconShapeKind::FilledPolygon, {0.5f, 0, 0, 0, 0}, 169, 17, true},
};

constexpr IconShape ICON_SHAPES_CONFIG[] = {
    {IconShapeKind::Segment, {40.0f, 36.875f, 40.0f, 74.375f, 0.0f}, 0, 0, false},
    {IconShapeKind::Segment, {40.0f, 5.625f, 40.0f, 24.375f, 0.0f}, 0, 0, false},
    {IconShapeKind::Segment, {68.125f, 68.125f, 68.125f, 74.375f, 0.0f}, 0, 0, false},
    {IconShapeKind::Segment, {68.125f, 5.625f, 68.125f, 55.625f, 0.0f}, 0, 0, false},
    {IconShapeKind::Segment, {77.5f, 55.625f, 58.75f, 55.625f, 0.0f}, 0, 0, false},
    {IconShapeKind::Segment, {11.875f, 55.625f, 11.875f, 74.375f, 0.0f}, 0, 0, false},
    {IconShapeKind::Segment, {11.875f, 5.625f, 11.875f, 43.125f, 0.0f}, 0, 0, false},
    {IconShapeKind::Segment, {2.5f, 43.125f, 21.25f, 43.125f, 0.0f}, 0, 0, false},
    {IconShapeKind::Segment, {49.375f, 24.375f, 30.625f, 24.375f, 0.0f}, 0, 0, false},
};

    constexpr IconShape ICON_SHAPES_MUTE[] = {
        {IconShapeKind::Polyline, {0, 0, 0, 0, 0}, 186, 6, true},
        {IconShapeKind::Arc, {14.0f, 14.0f, 5.5f, -52.0f, 52.0f}, 0, 0, false},
        {IconShapeKind::Arc, {14.0f, 14.0f, 9.0f, -58.0f, 58.0f}, 0, 0, false},
        {IconShapeKind::Segment, {5.0f, 5.0f, 23.0f, 23.0f, 0.0f}, 0, 0, false},
    };
constexpr IconShape ICON_SHAPES_FOLDER[]={
        {IconShapeKind::Polyline, {0, 0, 0, 0, 0}, 192, 6, false},
        {IconShapeKind::Polyline, {0, 0, 0, 0, 0}, 198, 6, false},
        {IconShapeKind::Polyline, {0, 0, 0, 0, 0}, 204, 3, false},
        {IconShapeKind::Polyline, {0, 0, 0, 0, 0}, 207, 6, false},
        {IconShapeKind::Polyline, {0, 0, 0, 0, 0}, 213, 2, false},
    };
constexpr IconShape ICON_SHAPES_FOLDER_UP[]={
        {IconShapeKind::Polyline, {0, 0, 0, 0, 0}, 215, 3, false},
        {IconShapeKind::Polyline, {0, 0, 0, 0, 0}, 218, 6, false},
        {IconShapeKind::Polyline, {0, 0, 0, 0, 0}, 224, 6, false},
        {IconShapeKind::Polyline, {0, 0, 0, 0, 0}, 230, 4, false},
    };
constexpr IconDefinition ICON_DEFINITIONS[] = {
    {ICON_SHAPES_RADIO, 8, 2.5f},
    {ICON_SHAPES_PLAYER, 15, 0.0f},
    {ICON_SHAPES_CONFIG, 9, 2.5f},
    {ICON_SHAPES_MUTE,4,1.9f},
    {ICON_SHAPES_FOLDER,5,1.55f},
    {ICON_SHAPES_FOLDER_UP,4,1.4f}
};
constexpr uint8_t ICON_MAX_SHAPE_COUNT = 15;
static_assert(sizeof(ICON_DEFINITIONS) / sizeof(ICON_DEFINITIONS[0]) == static_cast<unsigned>(IconId::Count));
}

#endif
