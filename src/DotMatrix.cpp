#include "DotMatrix.h"
#include "UiCanvas.h"
#include "StringUtils.h"

#include <cstring>
#include <cstdlib>
#include <algorithm>

// Eine Zeichenzeile aus fünf Bits: b4 ist die linke Spalte 0, b0 die rechte Spalte 4
#define R(b4, b3, b2, b1, b0) ((b4 << 4) | (b3 << 3) | (b2 << 2) | (b1 << 1) | b0)

/**
 * @brief HD44780 Font-Daten im PROGMEM für ASCII 0x21-0x7E (94 Zeichen), Index = Codepoint - 0x21. Authentischer HD44780-Zeichensatz, 5 Pixel breit, 7 Pixel hoch. Je Zeichen 7 Zeilen zu 5 Bits.
 */
static const uint8_t PROGMEM FONT_ASCII[94][7] = {
    // 0x21 !
    {
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 1, 0, 0)},
    // 0x22 "
    {
        R(0, 1, 0, 1, 0),
        R(0, 1, 0, 1, 0),
        R(0, 1, 0, 1, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0)},
    // 0x23 #
    {
        R(0, 1, 0, 1, 0),
        R(0, 1, 0, 1, 0),
        R(1, 1, 1, 1, 1),
        R(0, 1, 0, 1, 0),
        R(1, 1, 1, 1, 1),
        R(0, 1, 0, 1, 0),
        R(0, 1, 0, 1, 0)},
    // 0x24 $
    {
        R(0, 0, 1, 0, 0),
        R(0, 1, 1, 1, 1),
        R(1, 0, 1, 0, 0),
        R(0, 1, 1, 1, 0),
        R(0, 0, 1, 0, 1),
        R(1, 1, 1, 1, 0),
        R(0, 0, 1, 0, 0)},
    // 0x25 %
    {
        R(1, 1, 0, 0, 0),
        R(1, 1, 0, 0, 1),
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 0, 0),
        R(1, 0, 0, 1, 1),
        R(0, 0, 0, 1, 1)},
    // 0x26 &
    {
        R(0, 1, 1, 0, 0),
        R(1, 0, 0, 1, 0),
        R(1, 0, 1, 0, 0),
        R(0, 1, 0, 0, 0),
        R(1, 0, 1, 0, 1),
        R(1, 0, 0, 1, 0),
        R(0, 1, 1, 0, 1)},
    // 0x27 '
    {
        R(0, 0, 1, 1, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0)},
    // 0x28 (
    {
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 1, 0)},
    // 0x29 )
    {
        R(0, 1, 0, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 0, 0)},
    // 0x2A *
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 1, 0, 0),
        R(1, 0, 1, 0, 1),
        R(0, 1, 1, 1, 0),
        R(1, 0, 1, 0, 1),
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 0, 0)},
    // 0x2B +
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(1, 1, 1, 1, 1),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 0, 0)},
    // 0x2C ,
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 1, 1, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0)},
    // 0x2D -
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(1, 1, 1, 1, 1),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0)},
    // 0x2E .
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 1, 1, 0, 0),
        R(0, 1, 1, 0, 0)},
    // 0x2F /
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 1),
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0)},
    // 0x30 0
    {
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 1, 1),
        R(1, 0, 1, 0, 1),
        R(1, 1, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0)},
    // 0x31 1
    {
        R(0, 0, 1, 0, 0),
        R(0, 1, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 1, 1, 0)},
    // 0x32 2
    {
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(0, 0, 0, 0, 1),
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 0, 0),
        R(1, 1, 1, 1, 1)},
    // 0x33 3
    {
        R(1, 1, 1, 1, 1),
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0)},
    // 0x34 4
    {
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 1, 0),
        R(0, 1, 0, 1, 0),
        R(1, 0, 0, 1, 0),
        R(1, 1, 1, 1, 1),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 1, 0)},
    // 0x35 5
    {
        R(1, 1, 1, 1, 1),
        R(1, 0, 0, 0, 0),
        R(1, 1, 1, 1, 0),
        R(0, 0, 0, 0, 1),
        R(0, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0)},
    // 0x36 6
    {
        R(0, 0, 1, 1, 0),
        R(0, 1, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0)},
    // 0x37 7
    {
        R(1, 1, 1, 1, 1),
        R(0, 0, 0, 0, 1),
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0)},
    // 0x38 8
    {
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0)},
    // 0x39 9
    {
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 1),
        R(0, 0, 0, 0, 1),
        R(0, 0, 0, 1, 0),
        R(0, 1, 1, 0, 0)},
    // 0x3A :
    {
        R(0, 0, 0, 0, 0),
        R(0, 1, 1, 0, 0),
        R(0, 1, 1, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 1, 1, 0, 0),
        R(0, 1, 1, 0, 0),
        R(0, 0, 0, 0, 0)},
    // 0x3B ;
    {
        R(0, 0, 0, 0, 0),
        R(0, 1, 1, 0, 0),
        R(0, 1, 1, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 1, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 0, 0)},
    // 0x3C <
    {
        R(0, 0, 0, 0, 1),
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 0, 1)},
    // 0x3D =
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(1, 1, 1, 1, 1),
        R(0, 0, 0, 0, 0),
        R(1, 1, 1, 1, 1),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0)},
    // 0x3E >
    {
        R(1, 0, 0, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 0, 0),
        R(1, 0, 0, 0, 0)},
    // 0x3F ?
    {
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(0, 0, 0, 0, 1),
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 1, 0, 0)},
    // 0x40 @
    {
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 1, 1, 1),
        R(1, 0, 1, 0, 1),
        R(1, 0, 1, 1, 1),
        R(1, 0, 0, 0, 0),
        R(0, 1, 1, 1, 0)},
    // 0x41 A
    {
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 1, 1, 1, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1)},
    // 0x42 B
    {
        R(1, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 1, 1, 1, 0)},
    // 0x43 C
    {
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0)},
    // 0x44 D
    {
        R(1, 1, 1, 0, 0),
        R(1, 0, 0, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 1, 0),
        R(1, 1, 1, 0, 0)},
    // 0x45 E
    {
        R(1, 1, 1, 1, 1),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 1, 1, 1, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 1, 1, 1, 1)},
    // 0x46 F
    {
        R(1, 1, 1, 1, 1),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 1, 1, 1, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0)},
    // 0x47 G
    {
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 0),
        R(1, 0, 1, 1, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0)},
    // 0x48 H
    {
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 1, 1, 1, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1)},
    // 0x49 I
    {
        R(0, 1, 1, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 1, 1, 0)},
    // 0x4A J
    {
        R(0, 0, 1, 1, 1),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 1, 0),
        R(1, 0, 0, 1, 0),
        R(0, 1, 1, 0, 0)},
    // 0x4B K
    {
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 1, 0),
        R(1, 0, 1, 0, 0),
        R(1, 1, 0, 0, 0),
        R(1, 0, 1, 0, 0),
        R(1, 0, 0, 1, 0),
        R(1, 0, 0, 0, 1)},
    // 0x4C L
    {
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 1, 1, 1, 1)},
    // 0x4D M
    {
        R(1, 0, 0, 0, 1),
        R(1, 1, 0, 1, 1),
        R(1, 0, 1, 0, 1),
        R(1, 0, 1, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1)},
    // 0x4E N
    {
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 1, 0, 0, 1),
        R(1, 0, 1, 0, 1),
        R(1, 0, 0, 1, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1)},
    // 0x4F O
    {
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0)},
    // 0x50 P
    {
        R(1, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 1, 1, 1, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0)},
    // 0x51 Q
    {
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 1, 0, 1),
        R(1, 0, 0, 1, 0),
        R(0, 1, 1, 0, 1)},
    // 0x52 R
    {
        R(1, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 1, 1, 1, 0),
        R(1, 0, 1, 0, 0),
        R(1, 0, 0, 1, 0),
        R(1, 0, 0, 0, 1)},
    // 0x53 S
    {
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 0),
        R(0, 1, 1, 1, 0),
        R(0, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0)},
    // 0x54 T
    {
        R(1, 1, 1, 1, 1),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0)},
    // 0x55 U
    {
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0)},
    // 0x56 V
    {
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 0, 1, 0),
        R(0, 1, 0, 1, 0),
        R(0, 0, 1, 0, 0)},
    // 0x57 W
    {
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 1, 0, 1),
        R(1, 0, 1, 0, 1),
        R(1, 0, 1, 0, 1),
        R(0, 1, 0, 1, 0)},
    // 0x58 X
    {
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1)},
    // 0x59 Y
    {
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0)},
    // 0x5A Z
    {
        R(1, 1, 1, 1, 1),
        R(0, 0, 0, 0, 1),
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 1, 1, 1, 1)},
    // 0x5B [
    {
        R(0, 1, 1, 1, 0),
        R(0, 1, 0, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 1, 1, 1, 0)},
    // 0x5C backslash
    {
        R(0, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 0, 1),
        R(0, 0, 0, 0, 0)},
    // 0x5D ]
    {
        R(0, 1, 1, 1, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 1, 0),
        R(0, 1, 1, 1, 0)},
    // 0x5E ^
    {
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 1, 0),
        R(1, 0, 0, 0, 1),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0)},
    // 0x5F _
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(1, 1, 1, 1, 1)},
    // 0x60 `
    {
        R(0, 1, 0, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0)},
    // 0x61 a
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 1, 1, 1, 0),
        R(0, 0, 0, 0, 1),
        R(0, 1, 1, 1, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 1)},
    // 0x62 b
    {
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 1, 1, 0),
        R(1, 1, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 1, 1, 1, 0)},
    // 0x63 c
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(0, 1, 1, 1, 0)},
    // 0x64 d
    {
        R(0, 0, 0, 0, 1),
        R(0, 0, 0, 0, 1),
        R(0, 1, 1, 0, 1),
        R(1, 0, 0, 1, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 1)},
    // 0x65 e
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 1, 1, 1, 1),
        R(1, 0, 0, 0, 0),
        R(0, 1, 1, 1, 0)},
    // 0x66 f
    {
        R(0, 0, 1, 1, 0),
        R(0, 1, 0, 0, 1),
        R(0, 1, 0, 0, 0),
        R(1, 1, 1, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 1, 0, 0, 0)},
    // 0x67 g
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 1, 1, 1, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 1),
        R(0, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0)},
    // 0x68 h
    {
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 1, 1, 0),
        R(1, 1, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1)},
    // 0x69 i
    {
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 1, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 1, 1, 0)},
    // 0x6A j
    {
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 1, 1, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 1, 0),
        R(1, 0, 0, 1, 0),
        R(0, 1, 1, 0, 0)},
    // 0x6B k
    {
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 1, 0),
        R(1, 0, 1, 0, 0),
        R(1, 1, 0, 0, 0),
        R(1, 0, 1, 0, 0),
        R(1, 0, 0, 1, 0)},
    // 0x6C l
    {
        R(0, 1, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 1, 1, 0)},
    // 0x6D m
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(1, 1, 0, 1, 0),
        R(1, 0, 1, 0, 1),
        R(1, 0, 1, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1)},
    // 0x6E n
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(1, 0, 1, 1, 0),
        R(1, 1, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1)},
    // 0x6F o
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0)},
    // 0x70 p
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(1, 1, 1, 1, 0),
        R(1, 0, 0, 0, 1),
        R(1, 1, 1, 1, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0)},
    // 0x71 q
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 1, 1, 0, 1),
        R(1, 0, 0, 1, 1),
        R(0, 1, 1, 1, 1),
        R(0, 0, 0, 0, 1),
        R(0, 0, 0, 0, 1)},
    // 0x72 r
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(1, 0, 1, 1, 0),
        R(1, 1, 0, 0, 1),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0),
        R(1, 0, 0, 0, 0)},
    // 0x73 s
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 1, 1, 1, 0),
        R(1, 0, 0, 0, 0),
        R(0, 1, 1, 1, 0),
        R(0, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0)},
    // 0x74 t
    {
        R(0, 1, 0, 0, 0),
        R(0, 1, 0, 0, 0),
        R(1, 1, 1, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 1, 0, 0, 1),
        R(0, 0, 1, 1, 0)},
    // 0x75 u
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 1, 1),
        R(0, 1, 1, 0, 1)},
    // 0x76 v
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 0, 1, 0),
        R(0, 0, 1, 0, 0)},
    // 0x77 w
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(1, 0, 1, 0, 1),
        R(1, 0, 1, 0, 1),
        R(0, 1, 0, 1, 0)},
    // 0x78 x
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(1, 0, 0, 0, 1),
        R(0, 1, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 1, 0),
        R(1, 0, 0, 0, 1)},
    // 0x79 y
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(1, 0, 0, 0, 1),
        R(1, 0, 0, 0, 1),
        R(0, 1, 1, 1, 1),
        R(0, 0, 0, 0, 1),
        R(0, 1, 1, 1, 0)},
    // 0x7A z
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(1, 1, 1, 1, 1),
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 0, 0),
        R(1, 1, 1, 1, 1)},
    // 0x7B {
    {
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 1, 0)},
    // 0x7C |
    {
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0)},
    // 0x7D }
    {
        R(0, 1, 0, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 0, 1, 0),
        R(0, 0, 1, 0, 0),
        R(0, 0, 1, 0, 0),
        R(0, 1, 0, 0, 0)},
    // 0x7E ~
    {
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0),
        R(0, 1, 0, 0, 0),
        R(1, 0, 1, 0, 1),
        R(0, 0, 0, 1, 0),
        R(0, 0, 0, 0, 0),
        R(0, 0, 0, 0, 0)}};

/**
 * @brief Zeichen außerhalb von ASCII: Codepunkt und sieben Zeilen zu 5 Bits
 */
struct ExtendedChar
{
    uint16_t codepoint;
    uint8_t pattern[7];
};

static const ExtendedChar PROGMEM FONT_EXTENDED[] = {
    // 0x00B0 ° (Grad)
    {
        0x00B0, {R(0, 1, 1, 0, 0), R(1, 0, 0, 1, 0), R(0, 1, 1, 0, 0), R(0, 0, 0, 0, 0), R(0, 0, 0, 0, 0), R(0, 0, 0, 0, 0), R(0, 0, 0, 0, 0)}},
    // 0x00C4 Ä
    {
        0x00C4, {R(0, 1, 0, 1, 0), R(0, 1, 1, 1, 0), R(1, 0, 0, 0, 1), R(1, 1, 1, 1, 1), R(1, 0, 0, 0, 1), R(1, 0, 0, 0, 1), R(1, 0, 0, 0, 1)}},
    // 0x00C8 È
    {
        0x00C8, {R(0, 1, 0, 0, 0), R(1, 1, 1, 1, 1), R(1, 0, 0, 0, 0), R(1, 1, 1, 1, 0), R(1, 0, 0, 0, 0), R(1, 0, 0, 0, 0), R(1, 1, 1, 1, 1)}},
    // 0x00D6 Ö
    {
        0x00D6, {R(0, 1, 0, 1, 0), R(0, 1, 1, 1, 0), R(1, 0, 0, 0, 1), R(1, 0, 0, 0, 1), R(1, 0, 0, 0, 1), R(1, 0, 0, 0, 1), R(0, 1, 1, 1, 0)}},
    // 0x00DC Ü
    {
        0x00DC, {R(0, 1, 0, 1, 0), R(0, 0, 0, 0, 0), R(1, 0, 0, 0, 1), R(1, 0, 0, 0, 1), R(1, 0, 0, 0, 1), R(1, 0, 0, 0, 1), R(0, 1, 1, 1, 0)}},
    // 0x00DF ß
    {
        0x00DF, {R(0, 1, 1, 0, 0), R(1, 0, 0, 1, 0), R(1, 0, 0, 1, 0), R(1, 0, 1, 0, 0), R(1, 0, 0, 1, 0), R(1, 0, 0, 0, 1), R(1, 0, 1, 1, 0)}},
    // 0x00E4 ä
    {
        0x00E4, {R(0, 1, 0, 1, 0), R(0, 0, 0, 0, 0), R(0, 1, 1, 1, 0), R(0, 0, 0, 0, 1), R(0, 1, 1, 1, 1), R(1, 0, 0, 0, 1), R(0, 1, 1, 1, 1)}},
    // 0x00E8 è
    {
        0x00E8, {R(0, 1, 0, 0, 0), R(0, 0, 1, 0, 0), R(0, 1, 1, 1, 0), R(1, 0, 0, 0, 1), R(1, 1, 1, 1, 1), R(1, 0, 0, 0, 0), R(0, 1, 1, 1, 0)}},
    // 0x00F6 ö
    {
        0x00F6, {R(0, 1, 0, 1, 0), R(0, 0, 0, 0, 0), R(0, 1, 1, 1, 0), R(1, 0, 0, 0, 1), R(1, 0, 0, 0, 1), R(1, 0, 0, 0, 1), R(0, 1, 1, 1, 0)}},
    // 0x00FC ü
    {
        0x00FC, {R(0, 1, 0, 1, 0), R(0, 0, 0, 0, 0), R(1, 0, 0, 0, 1), R(1, 0, 0, 0, 1), R(1, 0, 0, 0, 1), R(1, 0, 0, 1, 1), R(0, 1, 1, 0, 1)}},
    // Endemarke
    {0x0000, {0, 0, 0, 0, 0, 0, 0}}};

#undef R

/** @brief Anzahl der Einträge in FONT_EXTENDED ohne Endemarke. */
static const int FONT_EXTENDED_COUNT = 10;

DotMatrix::DotMatrix()
    : _text(nullptr), _textLen(0), _textCapacity(0), _dotWidth(5), _dotHeight(5), _dotSpace(1), _charSpace(2) // Default: 2 Pixel zwischen Zeichen
      ,
      _onColor(0), _onShadow(0), _offColor(0), _offShadow(0), _bgColor(0) // Die Farben setzt der Aufrufer vor dem Zeichnen
      ,
      _showOffDots(true), _gradientEnabled(true), _dotShape(DotShape::SQUARE), _scrollEnabled(false), _scrollSpeed(1), _scrollOffset(0), _metricsDirty(true), _charCountCache(0), _textWidthCache(0), _scrollTimed(false), _scrollSpeedPps(30), _lastScrollMs(0), _scrollOffsetFP(0)
{
}

DotMatrix::~DotMatrix()
{
    if (_text)
    {
        free(_text);
        _text = nullptr;
    }
}

void DotMatrix::init(int dotWidth, int dotHeight, int dotSpace, int charSpace)
{
    _dotWidth = dotWidth;
    _dotHeight = dotHeight;
    _dotSpace = dotSpace;
    _charSpace = (charSpace < 0) ? (dotSpace * 2) : charSpace;
    _metricsDirty = true;
    recalcMetrics();
    resetScroll();
}

void DotMatrix::setOnColor(uint16_t color, uint16_t shadow)
{
    _onColor = color;
    _onShadow = shadow;
}

void DotMatrix::setOffColor(uint16_t color, uint16_t shadow)
{
    _offColor = color;
    _offShadow = shadow;
}

void DotMatrix::setBackgroundColor(uint16_t color)
{
    _bgColor = color;
}

void DotMatrix::setShowOffDots(bool show)
{
    _showOffDots = show;
}

void DotMatrix::setGradientEnabled(bool enable)
{
    _gradientEnabled = enable;
}

void DotMatrix::setDotShape(DotShape shape)
{
    _dotShape = shape;
}

void DotMatrix::setText(const char *text)
{
    if (!text)
    {
        if (_text)
            _text[0] = '\0';

        _textLen = 0;
        _metricsDirty = false;
        _charCountCache = 0;
        _textWidthCache = 0;

        resetScroll();
        return;
    }

    size_t len = strlen(text);

    // Die Kapazität verdoppelt sich, bis der Text samt Terminator passt. Schlägt realloc fehl, bleibt der bisherige Text erhalten
    if (len + 1 > _textCapacity)
    {
        size_t newCap = (_textCapacity == 0) ? (len + 1) : _textCapacity;
        while (newCap < len + 1)
            newCap *= 2;

        char *newBuf = (char *)realloc(_text, newCap);
        if (!newBuf)
            return;

        _text = newBuf;
        _textCapacity = newCap;
    }

    stringutils::safeCopy(_text, _textCapacity, text);
    _textLen = len;

    _metricsDirty = true;
    recalcMetrics();

    resetScroll();
}

bool DotMatrix::reserveText(size_t capacity)
{
    if (capacity == 0)
        return true;

    if (capacity <= _textCapacity)
        return true;

    char *newBuf = (char *)realloc(_text, capacity);
    if (!newBuf)
        return false;

    _text = newBuf;
    _textCapacity = capacity;

    // Ein noch leerer Puffer beginnt mit dem Terminator, damit getText() einen gültigen String liefert
    if (_textLen == 0)
        _text[0] = '\0';

    return true;
}

const char *DotMatrix::getText() const
{
    return _text ? _text : "";
}

void DotMatrix::setScrolling(bool enable, int speed)
{
    _scrollEnabled = enable;
    _scrollTimed = false;
    _scrollSpeed = (speed < 0) ? 0 : speed;

    if (!enable)
    {
        resetScroll();
    }
    else
    {
        _scrollOffsetFP = ((int32_t)_scrollOffset) << 16;
        _lastScrollMs = 0;
    }
}

void DotMatrix::setScrollingTimed(bool enable, int pixelsPerSecond)
{
    _scrollEnabled = enable;
    _scrollTimed = enable;
    _scrollSpeedPps = (pixelsPerSecond < 0) ? 0 : pixelsPerSecond;

    if (!enable)
    {
        resetScroll();
    }
    else
    {
        _lastScrollMs = 0;
        _scrollOffsetFP = ((int32_t)_scrollOffset) << 16;
    }
}

bool DotMatrix::updateScrollTimed(uint32_t nowMs)
{
    if (!_scrollEnabled || !_scrollTimed)
        return false;

    if (_lastScrollMs == 0)
    {
        _lastScrollMs = nowMs;
        return false;
    }

    uint32_t dt = nowMs - _lastScrollMs;
    _lastScrollMs = nowMs;

    if (dt == 0)
        return false;

    int totalWidth = getTextWidth();
    if (totalWidth <= 0 || _scrollSpeedPps <= 0)
        return false;

    // 16.16 Fixed-Point Schritt = px/s * ms
    int64_t stepFP = (int64_t)_scrollSpeedPps * (int64_t)dt * 65536LL / 1000LL;
    _scrollOffsetFP += (int32_t)stepFP;
    _scrollOffset = _scrollOffsetFP >> 16;

    if (_scrollOffset >= totalWidth)
    {
        int64_t mod = ((int64_t)totalWidth) << 16;
        if (mod > 0)
        {
            _scrollOffsetFP = (int32_t)((int64_t)_scrollOffsetFP % mod);
            _scrollOffset = _scrollOffsetFP >> 16;
        }

        return true;
    }

    return false;
}

void DotMatrix::resetScroll()
{
    _scrollOffset = 0;
    _scrollOffsetFP = 0;
    _lastScrollMs = 0;
}

bool DotMatrix::updateScroll()
{
    if (!_scrollEnabled || _scrollTimed)
        return false;

    if (_scrollSpeed <= 0)
        return false;

    _scrollOffset += _scrollSpeed;
    _scrollOffsetFP = ((int32_t)_scrollOffset) << 16;

    int totalWidth = getTextWidth();
    if (totalWidth <= 0)
    {
        resetScroll();
        return false;
    }

    if (_scrollOffset >= totalWidth)
    {
        resetScroll();
        return true; // Zyklus abgeschlossen
    }

    return false;
}

uint16_t DotMatrix::decodeUtf8(const char *&str) const
{
    if (!str || *str == '\0')
        return 0;

    uint8_t c = (uint8_t)*str++;

    if (c < 0x80)
        return c;

    if ((c & 0xE0) == 0xC0)
    {
        uint16_t cp = (c & 0x1F) << 6;
        if (*str)
            cp |= ((uint8_t)*str++ & 0x3F);
        return cp;
    }

    if ((c & 0xF0) == 0xE0)
    {
        uint16_t cp = (c & 0x0F) << 12;
        if (*str)
            cp |= ((uint8_t)*str++ & 0x3F) << 6;
        if (*str)
            cp |= ((uint8_t)*str++ & 0x3F);
        return cp;
    }

    // Nicht unterstütztes Startbyte als Fragezeichen ausgeben, Folgebytes werden separat verarbeitet
    return '?';
}

size_t DotMatrix::countChars(const char *str) const
{
    if (!str)
        return 0;

    size_t count = 0;
    while (*str)
    {
        decodeUtf8(str);
        count++;
    }

    return count;
}

void DotMatrix::recalcMetrics() const
{
    if (!_text || _textLen == 0)
    {
        _charCountCache = 0;
        _textWidthCache = 0;
        _metricsDirty = false;
        return;
    }

    size_t charCount = countChars(_text);

    int singleCharWidth = CHAR_COLS * (_dotWidth + _dotSpace) - _dotSpace;
    int width = (int)charCount * singleCharWidth + ((int)charCount - 1) * _charSpace;

    _charCountCache = charCount;
    _textWidthCache = width;
    _metricsDirty = false;
}

int DotMatrix::getTextWidth() const
{
    if (_metricsDirty)
        recalcMetrics();

    return _textWidthCache;
}

int DotMatrix::getHeight() const
{
    return CHAR_ROWS * (_dotHeight + _dotSpace) - _dotSpace;
}

int DotMatrix::getCharWidth() const
{
    int singleCharWidth = CHAR_COLS * (_dotWidth + _dotSpace) - _dotSpace;
    return singleCharWidth + _charSpace;
}

bool DotMatrix::getCharPattern(uint16_t codepoint, uint8_t pattern[7]) const
{
    const uint8_t *src = nullptr;

    if (codepoint >= 0x21 && codepoint <= 0x7E)
        src = FONT_ASCII[codepoint - 0x21];

    else if (codepoint == 0x20)
    {
        for (int i = 0; i < 7; i++)
            pattern[i] = 0;

        return true;
    }
    else
    {
        for (int i = 0; i < FONT_EXTENDED_COUNT; i++)
        {
            uint16_t cp = pgm_read_word(&FONT_EXTENDED[i].codepoint);
            if (cp == codepoint)
            {
                for (int j = 0; j < 7; j++)
                    pattern[j] = pgm_read_byte(&FONT_EXTENDED[i].pattern[j]);

                return true;
            }
        }
    }

    // Unbekannte Zeichen erscheinen als Fragezeichen
    if (!src)
        src = FONT_ASCII['?' - 0x21];

    for (int i = 0; i < 7; i++)
        pattern[i] = pgm_read_byte(&src[i]);

    return true;
}

void DotMatrix::drawDot(UiCanvas *sprite, int cx, int cy, bool isOn)
{
    uint16_t mainColor = isOn ? _onColor : _offColor;
    uint16_t shadowColor = isOn ? _onShadow : _offShadow;

    if (_dotShape == DotShape::SQUARE)
    {
        int x0 = cx - _dotWidth / 2;
        int y0 = cy - _dotHeight / 2;
        sprite->fillRect(x0, y0, _dotWidth, _dotHeight, mainColor);

        return;
    }

    int32_t r = std::min(_dotWidth, _dotHeight) / 2;

    // Sehr kleine Dots zeichnen ohne Verlauf, weil er dort nicht erkennbar wäre
    if (_dotWidth <= 3 || _dotHeight <= 3 || !_gradientEnabled)
    {
        sprite->fillCircle(cx, cy, r, mainColor);
        return;
    }

    // 3D-Effekt: Ein äußerer Kreis in der Schattenfarbe und darin ein hellerer, nach oben links versetzter Kreis
    sprite->fillSmoothCircle(cx, cy, r, shadowColor);

    int32_t innerR = (r * 65) / 100; // 65 % des Radius
    int offsetX = -(r * 18) / 100;   // 18 % Versatz
    int offsetY = -(r * 18) / 100;

    sprite->fillSmoothCircle(cx + offsetX, cy + offsetY, innerR, mainColor);
}

void DotMatrix::drawInternal(UiCanvas *sprite, int x, int y, int clipWidth)
{
    if (!_text || _textLen == 0)
        return;

    int charW = getCharWidth();
    int dotPitchX = _dotWidth + _dotSpace;
    int dotPitchY = _dotHeight + _dotSpace;

    int startX = x - (_scrollEnabled ? _scrollOffset : 0);

    // Marquee: Bei aktivem Scrolling + Clipping den Text zweimal zeichnen, damit beim Rausscrollen links sofort die Wiederholung rechts nachkommt
    const int totalWidth = getTextWidth();
    const int passes = (_scrollEnabled && clipWidth > 0 && totalWidth > 0) ? 2 : 1;

    for (int pass = 0; pass < passes; pass++)
    {
        const int passX = startX + pass * totalWidth;

        if (clipWidth > 0 && passX >= x + clipWidth)
            continue;

        const char *ptr = _text;
        int charIdx = 0;

        while (*ptr)
        {
            uint16_t codepoint = decodeUtf8(ptr);
            if (codepoint == 0)
                break;

            int charX = passX + charIdx * charW;

            // Zeichen ganz außerhalb des sichtbaren Bereichs: links überspringen, rechts abbrechen
            if (clipWidth > 0)
            {
                if (charX + charW < x)
                {
                    charIdx++;
                    continue;
                }
                if (charX >= x + clipWidth)
                    break;
            }

            uint8_t pattern[7];
            getCharPattern(codepoint, pattern);

            for (int row = 0; row < CHAR_ROWS; row++)
            {
                uint8_t rowBits = pattern[row];

                for (int col = 0; col < CHAR_COLS; col++)
                {
                    bool isOn = (rowBits >> (4 - col)) & 1;

                    int dotX = charX + col * dotPitchX + _dotWidth / 2;
                    int dotY = y + row * dotPitchY + _dotHeight / 2;

                    if (clipWidth > 0)
                    {
                        if (dotX - _dotWidth / 2 >= x + clipWidth)
                            continue;
                        if (dotX + _dotWidth / 2 < x)
                            continue;
                    }

                    if (isOn || _showOffDots)
                        drawDot(sprite, dotX, dotY, isOn);
                }
            }

            // Trenn-Spalte: Off-Dot in der charSpace-Lücke (durchgehendes VFD-Raster)
            if (_showOffDots && _charSpace >= _dotWidth)
            {
                int singleW = CHAR_COLS * dotPitchX - _dotSpace;
                int sepX = charX + singleW + _charSpace / 2;

                if (clipWidth <= 0 ||
                    (sepX - _dotWidth / 2 < x + clipWidth && sepX + _dotWidth / 2 >= x))
                {
                    for (int row = 0; row < CHAR_ROWS; row++)
                    {
                        int dotY = y + row * dotPitchY + _dotHeight / 2;
                        drawDot(sprite, sepX, dotY, false);
                    }
                }
            }

            charIdx++;
        }
    }
}

void DotMatrix::draw(UiCanvas *sprite, int x, int y)
{
    drawInternal(sprite, x, y, -1);
}

void DotMatrix::drawClipped(UiCanvas *sprite, int x, int y, int clipWidth)
{
    drawInternal(sprite, x, y, clipWidth);
}
