#include "Thema.h"

namespace
{
    Thema s_modern = ThemaModernVorgabe;                                     ///< Modern mit den beim Start geladenen Grundfarben
    const Thema *const Themen[ThemenAnzahl] = {&ThemaRetro, &s_modern, &ThemaEPaper}; ///< Themen nach Index
    const Thema *s_aktiv = &ThemaRetro;                                      ///< Aktives Thema, ab dem Start fest
}

void setzeModernFarben(const ModernFarben &farben)
{
    s_modern = baueModern(farben);
}

void setzeThema(uint8_t index)
{
    s_aktiv = &themaNachIndex(index);
}

const Thema &themaNachIndex(uint8_t index)
{
    return index < ThemenAnzahl && Themen[index] ? *Themen[index] : ThemaRetro;
}

const Thema &themaAktiv()
{
    return *s_aktiv;
}
