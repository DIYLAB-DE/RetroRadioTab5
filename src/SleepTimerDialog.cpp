#include "SleepTimerDialog.h"

#include <Arduino.h>

#include "Logger.h"
#include "StringUtils.h"
#include "Thema.h"

#include "fonts/ComfortaaBold21.h"
#include "fonts/ComfortaaBold27.h"
#include "fonts/LeagueGothicCond51.h"

SleepTimerDialog::SleepTimerDialog(RadioDialogCanvas *gfx) : m_tft(gfx)
{
}

void SleepTimerDialog::setBounds(int x, int y, int w, int h)
{
    dialogX = x;
    dialogY = y;
    dialogW = w;
    dialogH = h;
}

void SleepTimerDialog::setTitle(const char *title)
{
    titleText = title ? title : "Sleep-Timer";
}

void SleepTimerDialog::setMessage(const char *message)
{
    messageText = message ? message : "";
}

void SleepTimerDialog::setCancelLabel(const char *label)
{
    if (!label)
        return;
    stringutils::safeCopy(cancelLabel, label);
}

void SleepTimerDialog::setStopLabel(const char *label)
{
    if (!label)
        return;
    stringutils::safeCopy(stopLabel, label);
}

void SleepTimerDialog::setOptions(const char *labels[], const int values[], int count)
{
    if (!labels || !values || count <= 0)
    {
        buttonCount = 0;
        return;
    }

    buttonCount = (count > MAX_TIMER_OPTIONS) ? MAX_TIMER_OPTIONS : count;

    for (int i = 0; i < buttonCount; ++i)
    {
        buttonLabels[i] = labels[i] ? labels[i] : "";
        buttonValues[i] = values[i];
    }
}

void SleepTimerDialog::setCallback(std::function<void(int)> cb)
{
    callback = cb;
}

void SleepTimerDialog::setTimeout(uint32_t millis)
{
    timeoutMs = millis;
}

void SleepTimerDialog::startSleepCountdown(int minutes, std::function<void()> onTimeout)
{
    if (minutes <= 0)
    {
        cancelSleepTimer();
        return;
    }

    sleepTimerStart = millis();
    sleepTimerDuration = static_cast<uint32_t>(minutes) * 60000UL;
    sleepTimerActive = true;
    timeoutCallback = onTimeout;
}

void SleepTimerDialog::start()
{
    state = Active;
    touchState = TouchState();
    dialogStartTime = millis();

    draw();
}

bool SleepTimerDialog::isActive() const
{
    return state == Active;
}

bool SleepTimerDialog::isTimerActive() const
{
    return sleepTimerActive;
}

void SleepTimerDialog::getRemainingTime(int &minutes, int &seconds) const
{
    if (!sleepTimerActive)
    {
        minutes = -1;
        seconds = -1;
        return;
    }

    uint32_t elapsed = millis() - sleepTimerStart;
    if (elapsed >= sleepTimerDuration)
    {
        minutes = 0;
        seconds = 0;
        return;
    }

    uint32_t remaining = (sleepTimerDuration - elapsed) / 1000;
    minutes = remaining / 60;
    seconds = remaining % 60;
}

void SleepTimerDialog::update()
{
    // Der Countdown läuft auch bei geschlossenem Dialog weiter
    checkSleepTimer();

    if (state == Active)
    {
        processTouchInput();
        checkDialogTimeout();
    }
}

void SleepTimerDialog::cancelSleepTimer()
{
    if (sleepTimerActive)
    {
        sleepTimerActive = false;
        timeoutCallback = nullptr;
    }
}

void SleepTimerDialog::draw()
{
    m_tft->startWrite();

    drawFrame();
    drawHeader();
    drawSeparator();
    drawMessage();
    drawTimerButtons();
    drawControlButtons();

    m_tft->endWrite();
}

void SleepTimerDialog::drawFrame()
{
    m_tft->fillSmoothRoundRect(dialogX, dialogY, dialogW, dialogH, UIConfig::BORDER_RADIUS, farbe(Rolle::GemDialogRahmen));
    m_tft->fillSmoothRoundRect(dialogX + UIConfig::BORDER, dialogY + UIConfig::BORDER, dialogW - 2 * UIConfig::BORDER, dialogH - 2 * UIConfig::BORDER, UIConfig::BORDER_RADIUS, farbe(Rolle::GemGrund));
}

void SleepTimerDialog::drawHeader()
{
    int headerX = dialogX + UIConfig::PADDING;
    int headerY = dialogY + UIConfig::TITLE_Y_OFFSET;

    m_tft->loadFont(ComfortaaBold27);
    m_tft->setTextDatum(TL_DATUM);
    m_tft->setTextColor(farbe(Rolle::GemTitel), farbe(Rolle::GemGrund));
    m_tft->drawString(titleText, headerX, headerY + UIConfig::TITLE_Y_SHIFT);
    m_tft->unloadFont();
}

void SleepTimerDialog::drawSeparator()
{
    m_tft->fillRect(dialogX + UIConfig::PADDING, dialogY + UIConfig::SEPARATOR_Y, dialogW - 2 * UIConfig::PADDING, UIConfig::SEPARATOR_HEIGHT, farbe(Rolle::GemDialogTrenner));
}

void SleepTimerDialog::drawMessage()
{
    if (messageText && *messageText)
    {
        m_tft->loadFont(ComfortaaBold21);
        m_tft->setTextDatum(TL_DATUM);
        m_tft->setTextColor(farbe(Rolle::GemTextSilber), farbe(Rolle::GemGrund));
        m_tft->drawString(messageText, dialogX + UIConfig::PADDING, dialogY + UIConfig::MESSAGE_Y);
        m_tft->unloadFont();
    }
}

void SleepTimerDialog::drawTimerButtons()
{
    int contentWidth = dialogW - 2 * UIConfig::PADDING;
    int btnW = (contentWidth - (UIConfig::MATRIX_COLS - 1) * UIConfig::BUTTON_SPACING) / UIConfig::MATRIX_COLS;

    // Die Matrix sitzt über den Steuertasten und endet mit Abstand vor ihnen
    int controlButtonsHeight = UIConfig::BUTTON_HEIGHT + UIConfig::BUTTON_SPACING;
    int matrixHeight = UIConfig::MATRIX_ROWS * UIConfig::BUTTON_HEIGHT + (UIConfig::MATRIX_ROWS - 1) * UIConfig::BUTTON_SPACING;
    int startY = dialogY + dialogH - UIConfig::PADDING - controlButtonsHeight - matrixHeight - UIConfig::BUTTON_SPACING;
    const uint16_t rahmen = farbe(Rolle::GemRahmen), flaeche = farbe(Rolle::GemTastenFlaeche), text = farbe(Rolle::GemTextGedaempft);

    for (int i = 0; i < buttonCount && i < 6; ++i)
    {
        int col = i % UIConfig::MATRIX_COLS;
        int row = i / UIConfig::MATRIX_COLS;

        int x = dialogX + UIConfig::PADDING + col * (btnW + UIConfig::BUTTON_SPACING);
        int y = startY + row * (UIConfig::BUTTON_HEIGHT + UIConfig::BUTTON_SPACING);

        buttons[i].initButton(
            m_tft,
            x + btnW / 2,
            y + UIConfig::BUTTON_HEIGHT / 2,
            btnW,
            UIConfig::BUTTON_HEIGHT,
            UIConfig::BORDER_RADIUS,
            rahmen,
            flaeche,
            text,
            buttonLabels[i],
            nullptr);

        buttons[i].setFont(LeagueGothicCond51);
        buttons[i].setBorderWidths(UIConfig::BUTTON_BORDER, UIConfig::BUTTON_BORDER_PRESSED);
        buttons[i].drawButton();
    }
}

void SleepTimerDialog::drawControlButtons()
{
    int contentWidth = dialogW - 2 * UIConfig::PADDING;
    int btnY = dialogY + dialogH - UIConfig::PADDING - UIConfig::BUTTON_HEIGHT;
    const uint16_t rahmen = farbe(Rolle::GemRahmen), flaeche = farbe(Rolle::GemTastenFlaeche), abbruchText = farbe(Rolle::GemText);

    if (sleepTimerActive)
    {
        // Stopp links, Abbruch rechts, je halbe Breite
        int btnW = (contentWidth - UIConfig::BUTTON_SPACING) / 2;

        btnStopTimer.initButton(
            m_tft,
            dialogX + UIConfig::PADDING + btnW / 2,
            btnY + UIConfig::BUTTON_HEIGHT / 2,
            btnW,
            UIConfig::BUTTON_HEIGHT,
            UIConfig::BORDER_RADIUS,
            farbe(Rolle::GemWarnung),
            flaeche,
            farbe(Rolle::GemWarnung),
            stopLabel,
            nullptr);
        btnStopTimer.setFont(LeagueGothicCond51);
        btnStopTimer.setBorderWidths(UIConfig::BUTTON_BORDER, UIConfig::BUTTON_BORDER_PRESSED);
        btnStopTimer.drawButton();

        btnCancel.initButton(
            m_tft,
            dialogX + UIConfig::PADDING + btnW + UIConfig::BUTTON_SPACING + btnW / 2,
            btnY + UIConfig::BUTTON_HEIGHT / 2,
            btnW,
            UIConfig::BUTTON_HEIGHT,
            UIConfig::BORDER_RADIUS,
            rahmen,
            flaeche,
            abbruchText,
            cancelLabel,
            nullptr);
    }
    else
    {
        // Ohne Countdown füllt die Abbruchtaste die ganze Breite
        btnCancel.initButton(
            m_tft,
            dialogX + dialogW / 2,
            btnY + UIConfig::BUTTON_HEIGHT / 2,
            contentWidth,
            UIConfig::BUTTON_HEIGHT,
            UIConfig::BORDER_RADIUS,
            rahmen,
            flaeche,
            abbruchText,
            cancelLabel,
            nullptr);
    }

    btnCancel.setFont(LeagueGothicCond51);
    btnCancel.setBorderWidths(UIConfig::BUTTON_BORDER, UIConfig::BUTTON_BORDER_PRESSED);
    btnCancel.drawButton();
}

void SleepTimerDialog::processTouchInput()
{
    lgfx::touch_point_t touch;
    bool currentlyTouching = m_tft->getTouch(&touch);

    if (currentlyTouching)
    {
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
        if (touchState.activeButton >= 0 && touchState.activeButton < buttonCount)
        {
            closeDialog(buttonValues[touchState.activeButton]);
        }
        else if (touchState.activeButton == 98)
        {
            cancelSleepTimer();
            closeDialog(-2);
        }
        else if (touchState.activeButton == 99)
        {
            closeDialog(-1);
        }
        else
        {
            updateButtonHighlight(-1);
        }

        touchState = TouchState();
    }
}

void SleepTimerDialog::checkDialogTimeout()
{
    if (timeoutMs > 0 && (millis() - dialogStartTime) > timeoutMs)
        closeDialog(-1);
}

void SleepTimerDialog::checkSleepTimer()
{
    if (sleepTimerActive && (millis() - sleepTimerStart >= sleepTimerDuration))
    {
        sleepTimerActive = false;
        if (timeoutCallback)
        {
            auto cb = timeoutCallback;
            timeoutCallback = nullptr;
            cb();
        }
    }
}

int SleepTimerDialog::findButtonAtPosition(int x, int y)
{
    for (int i = 0; i < buttonCount; ++i)
        if (buttons[i].contains(x, y))
            return i;

    if (sleepTimerActive && btnStopTimer.contains(x, y))
        return 98;

    if (btnCancel.contains(x, y))
        return 99;

    return -1;
}

void SleepTimerDialog::updateButtonHighlight(int newActiveButton)
{
    int oldActiveButton = touchState.activeButton;

    if (oldActiveButton == newActiveButton)
        return;

    if (oldActiveButton >= 0 && oldActiveButton < buttonCount)
        buttons[oldActiveButton].drawButton(false);
    else if (oldActiveButton == 98 && sleepTimerActive)
        btnStopTimer.drawButton(false);
    else if (oldActiveButton == 99)
        btnCancel.drawButton(false);

    if (newActiveButton >= 0 && newActiveButton < buttonCount)
        buttons[newActiveButton].drawButton(true);
    else if (newActiveButton == 98 && sleepTimerActive)
        btnStopTimer.drawButton(true);
    else if (newActiveButton == 99)
        btnCancel.drawButton(true);
}

void SleepTimerDialog::closeDialog(int result)
{
    state = Inactive;

    // Der Rückruf wird vor dem Aufruf abgelöst, damit er höchstens einmal läuft
    if (callback)
    {
        auto cb = callback;
        callback = nullptr;
        cb(result);
    }
}
