#include "RadioPresetDialog.h"

#include <Arduino.h>

#include "Logger.h"
#include "Thema.h"
#include "StationManager.h" // Station-Definition für den Zugriff in start()

#include "fonts/ComfortaaBold27.h"
#include "fonts/LeagueGothicCond51.h"

RadioPresetDialog::RadioPresetDialog(RadioDialogCanvas *gfx) : m_tft(gfx)
{
}

void RadioPresetDialog::setBounds(int x, int y, int w, int h)
{
    dialogX = x;
    dialogY = y;
    dialogW = w;
    dialogH = h;
}

void RadioPresetDialog::setTitle(const char *title)
{
    titleText = title ? title : "Favoriten";
}

void RadioPresetDialog::setCallback(std::function<void(int)> cb)
{
    callback = cb;
}

void RadioPresetDialog::setTimeout(uint32_t millis)
{
    timeoutMs = millis;
}

void RadioPresetDialog::start(Station *stations, int count)
{
    if (dialogW <= 0 || dialogH <= 0)
    {
        LOG_ERROR_F("RadioPresetDialog: Ungültige Dialog-Größe (%d x %d)", dialogW, dialogH);
        return;
    }

    state = Active;
    selectedIndex = -1;
    touchState = TouchState();
    presetCount = 0;
    startTime = millis();

    for (int i = 0; i < count && presetCount < MAX_PRESETS; ++i)
    {
        if (stations[i].preset)
        {
            presetEntries[presetCount].name = stations[i].name;
            presetEntries[presetCount].stationIndex = i;
            presetCount++;
        }
    }

    draw();
}

bool RadioPresetDialog::isActive() const
{
    return state == Active;
}

int RadioPresetDialog::getSelectedIndex() const
{
    return selectedIndex;
}

void RadioPresetDialog::update()
{
    if (state != Active)
        return;

    processTouchInput();
    checkTimeout();
}

void RadioPresetDialog::draw()
{
    m_tft->startWrite();

    drawFrame();
    drawHeader();
    drawSeparator();
    drawPresetButtons();
    drawCancelButton();

    m_tft->endWrite();
}

void RadioPresetDialog::drawFrame()
{
    m_tft->fillSmoothRoundRect(dialogX, dialogY, dialogW, dialogH, UIConfig::BORDER_RADIUS, farbe(Rolle::GemDialogRahmen));
    m_tft->fillSmoothRoundRect(dialogX + UIConfig::BORDER, dialogY + UIConfig::BORDER, dialogW - 2 * UIConfig::BORDER, dialogH - 2 * UIConfig::BORDER, UIConfig::BORDER_RADIUS, farbe(Rolle::GemGrund));
}

void RadioPresetDialog::drawHeader()
{
    int headerX = dialogX + UIConfig::PADDING;
    int headerY = dialogY + UIConfig::TITLE_Y_OFFSET;

    m_tft->loadFont(ComfortaaBold27);
    m_tft->setTextColor(farbe(Rolle::GemTitel), farbe(Rolle::GemGrund));
    m_tft->drawString(titleText, headerX, headerY + UIConfig::TITLE_Y_SHIFT);
    m_tft->unloadFont();
}

void RadioPresetDialog::drawSeparator()
{
    m_tft->fillRect(dialogX + UIConfig::PADDING, dialogY + UIConfig::SEPARATOR_Y, dialogW - 2 * UIConfig::PADDING, UIConfig::SEPARATOR_HEIGHT, farbe(Rolle::GemDialogTrenner));
}

void RadioPresetDialog::drawPresetButtons()
{
    if (presetCount == 0)
    {
        int centerX = dialogX + dialogW / 2;
        int centerY = dialogY + UIConfig::CONTENT_START_Y + UIConfig::EMPTY_HINT_Y;

        m_tft->loadFont(ComfortaaBold27);
        m_tft->setTextColor(farbe(Rolle::GemTextGedaempft), farbe(Rolle::GemGrund));
        m_tft->setTextDatum(middle_center);
        m_tft->drawString("Keine Favoriten vorhanden", centerX, centerY);
        m_tft->setTextDatum(top_left);
        m_tft->unloadFont();
        return;
    }

    int contentWidth = dialogW - 2 * UIConfig::PADDING;
    int columnWidth = (contentWidth - UIConfig::BUTTON_SPACING) / 2;
    int startY = dialogY + UIConfig::CONTENT_START_Y;
    const uint16_t rahmen = farbe(Rolle::DlgFavoritTasteRahmen), flaeche = farbe(Rolle::GemTastenFlaeche), text = farbe(Rolle::GemTextGedaempft);

    for (int i = 0; i < presetCount; ++i)
    {
        int column = i / UIConfig::BUTTONS_PER_COLUMN;
        int row = i % UIConfig::BUTTONS_PER_COLUMN;

        int x = dialogX + UIConfig::PADDING + column * (columnWidth + UIConfig::BUTTON_SPACING);
        int y = startY + row * (UIConfig::BUTTON_HEIGHT + UIConfig::BUTTON_SPACING);

        presetButtons[i].initButton(
            m_tft,
            x + columnWidth / 2,             // Mitte X
            y + UIConfig::BUTTON_HEIGHT / 2, // Mitte Y
            columnWidth,
            UIConfig::BUTTON_HEIGHT,
            UIConfig::BORDER_RADIUS,
            rahmen,
            flaeche,
            text,
            presetEntries[i].name,
            nullptr);

        presetButtons[i].setFont(ComfortaaBold27);
        presetButtons[i].setLabelDatum(0, 0, textdatum_t::middle_left);
        presetButtons[i].setBorderWidths(UIConfig::BUTTON_BORDER, UIConfig::BUTTON_BORDER_PRESSED);
        presetButtons[i].setInsets(UIConfig::BUTTON_INSET, UIConfig::BUTTON_ICON_GAP);
        presetButtons[i].setIconSize(UIConfig::NUMBER_CIRCLE_SIZE, UIConfig::NUMBER_CIRCLE_SIZE);
        presetButtons[i].setIconPosition(UiButton::IconPosition::Left);
        presetButtons[i].enableNumberCircle(i + 1);
        presetButtons[i].drawButton();
    }
}

void RadioPresetDialog::drawCancelButton()
{
    int btnWidth = dialogW - 2 * UIConfig::PADDING;
    int btnX = dialogX + UIConfig::PADDING;
    int btnY = dialogY + dialogH - UIConfig::PADDING - UIConfig::CANCEL_BUTTON_HEIGHT;

    btnCancel.initButton(
        m_tft,
        btnX + btnWidth / 2,                       // Mitte X
        btnY + UIConfig::CANCEL_BUTTON_HEIGHT / 2, // Mitte Y
        btnWidth,
        UIConfig::CANCEL_BUTTON_HEIGHT,
        UIConfig::BORDER_RADIUS,
        farbe(Rolle::GemRahmen),
        farbe(Rolle::GemTastenFlaeche),
        farbe(Rolle::GemText),
        "abbruch",
        nullptr);

    btnCancel.setFont(LeagueGothicCond51);
    btnCancel.setBorderWidths(UIConfig::BUTTON_BORDER, UIConfig::BUTTON_BORDER_PRESSED);
    btnCancel.drawButton();
}

void RadioPresetDialog::checkTimeout()
{
    if (timeoutMs > 0 && (millis() - startTime) > timeoutMs)
        finish(-1);
}

void RadioPresetDialog::processTouchInput()
{
    lgfx::touch_point_t touch;
    bool currentlyTouching = m_tft->getTouch(&touch);

    if (currentlyTouching)
    {
        // Jede Berührung setzt den Timeout zurück
        startTime = millis();

        int buttonIndex = findButtonAtPosition(touch.x, touch.y);

        if (buttonIndex != touchState.activeButton)
        {
            updateButtonHighlight(buttonIndex);
            touchState.activeButton = buttonIndex;
        }

        touchState.isTouching = true;
    }
    else if (touchState.isTouching)
    {
        // Beim Loslassen gilt die zuletzt gedrückte Taste
        if (touchState.activeButton >= 0 && touchState.activeButton < presetCount)
            finish(presetEntries[touchState.activeButton].stationIndex);
        else if (touchState.activeButton == 99)
            finish(-1);
        else
            updateButtonHighlight(-1);

        touchState = TouchState();
    }
}

int RadioPresetDialog::findButtonAtPosition(int x, int y)
{
    for (int i = 0; i < presetCount; ++i)
        if (presetButtons[i].contains(x, y))
            return i;

    if (btnCancel.contains(x, y))
        return 99;

    return -1;
}

void RadioPresetDialog::updateButtonHighlight(int newActiveButton)
{
    // touchState.activeButton enthält noch die alte Taste
    int oldActiveButton = touchState.activeButton;

    if (oldActiveButton != newActiveButton)
    {
        if (oldActiveButton >= 0 && oldActiveButton < presetCount)
            presetButtons[oldActiveButton].drawButton(false);
        else if (oldActiveButton == 99)
            btnCancel.drawButton(false);
    }

    if (newActiveButton >= 0 && newActiveButton < presetCount)
        presetButtons[newActiveButton].drawButton(true);
    else if (newActiveButton == 99)
        btnCancel.drawButton(true);
}

void RadioPresetDialog::finish(int result)
{
    selectedIndex = result;
    state = Inactive;

    // Der Rückruf wird vor dem Aufruf abgelöst, damit er höchstens einmal läuft
    if (callback)
    {
        auto cb = callback;
        callback = nullptr;
        cb(selectedIndex);
    }
}
