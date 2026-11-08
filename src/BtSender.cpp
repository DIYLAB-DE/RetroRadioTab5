#include "BtSender.h"
#include "Logger.h"
#include <Arduino.h>
#include <algorithm>
#include <cstdio>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp32-hal-periman.h>
#include <esp_heap_caps.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include <soc/i2s_periph.h>

namespace
{
    constexpr uart_port_t Port = UART_NUM_1;                   ///< UART0 trägt die Konsole an G37/G38, UART1 bis UART4 nutzt die Firmware sonst nicht
    constexpr int RxBufferBytes = 512;                         ///< Empfangspuffer des Treibers, muss größer als die Hardware-FIFO mit 128 Byte sein. Der Task leert ihn alle 20 ms
    constexpr uint32_t StackBytes = 6144;                      ///< Stack des Tasks im PSRAM
    constexpr int IdleSamples = 8;                             ///< Ablesungen je Ruhepegel, alle müssen gleich sein
    constexpr int AwakeSamples = 8;                            ///< Ablesungen in picoAwake(), eine mit High genügt
    constexpr uint32_t AwakeSampleUs = 100;                    ///< Abstand der Ablesungen in picoAwake(), länger als ein UART-Zeichen mit 115200 Baud (87 µs)
    constexpr TickType_t HalfPeriod = pdMS_TO_TICKS(2);        ///< Abstand zweier Flanken. vTaskDelay wartet bis zum übernächsten Tick, also mindestens 1 ms
    constexpr TickType_t UartSettle = pdMS_TO_TICKS(2);        ///< Ruhepegel High vor dem ersten Byte, damit der Pico die vorangehende Low-Phase abschließt
    constexpr gpio_drive_cap_t WeakDrive = GPIO_DRIVE_CAP_0;   ///< Schwächste Treiberstufe statt der Vorgabe 2 für jede getriebene Leitung. Begrenzt den Strom, falls eine Leitung doch an Masse oder einem Ausgang liegt (nicht gemessen)
    constexpr TickType_t LinkPoll = pdMS_TO_TICKS(20);         ///< Takt des Tasks bei offener UART: Befehle senden, Zeilen lesen
    constexpr TickType_t ProbeWait = pdMS_TO_TICKS(1000);      ///< Abstand der Ruhepegelprüfung bei geschlossener UART
    constexpr uint32_t HelloRetryMs = 5000;                    ///< Wartezeit nach einer HALLO-Anfrage ohne Antwort, bevor das Tab5 G6 erneut treibt
    constexpr uint32_t PresenceIntervalMs = 500;               ///< Abstand der Prüfung, ob G7 bei offener UART noch High ruht
    constexpr uint8_t LowChecksToClose = 2;                    ///< So viele Prüfungen hintereinander mit G7 dauerhaft Low schließen die UART
    constexpr UBaseType_t OrderSlots = 4;                      ///< Plätze der Befehlsqueue
    constexpr const char *CommandLines[] = {BtProtocol::Search, BtProtocol::Connect, BtProtocol::Disconnect, BtProtocol::Forget, BtProtocol::Query, BtProtocol::Tone, BtProtocol::Ukw}; ///< Befehlszeile je BtSender::Command, bei Connect folgt die Adresse
    constexpr int AudioPort = 0;                               ///< audio-lite gibt über I2S0 aus (Audio-Konstruktor mit I2S_NUM_0)
    constexpr uint8_t TapSources[BtProtocol::LineCount] = {Tab5Board::I2sBclk, Tab5Board::I2sLrck, Tab5Board::I2sData}; ///< Codec-Pins, deren Signale der Tonweg abgreift, in der Reihenfolge von BtProtocol::LinePins

    /** @brief Ausgangssignal von I2S0 je Leitung in der Reihenfolge von BtProtocol::LinePins: Bittakt und Wortauswahl des sendenden Masters, erste Datenleitung. Dieselben Signale verbindet der I2S-Treiber mit den Codec-Pins. */
    uint32_t tapSignal(size_t line)
    {
        const i2s_signal_conn_t &port = i2s_periph_signal[AudioPort];
        return line == 0 ? port.m_tx_bck_sig : line == 1 ? port.m_tx_ws_sig
                                                         : port.data_out_sigs[0];
    }

    /** @brief Liest die Konfiguration von pin. @return true, wenn der Pin weder treibt noch einem Arduino-Treiber gehört. Ein Ausgangssignal einer Peripherie zählt als treibend. */
    bool notDriven(uint8_t pin, gpio_io_config_t &config)
    {
        if (gpio_get_io_config(static_cast<gpio_num_t>(pin), &config) != ESP_OK)
            return false;
        return perimanGetPinBusType(pin) == ESP32_BUS_TYPE_INIT && !config.oe && (!config.oe_ctrl_by_periph || config.sig_out == SIG_GPIO_OUT_IDX);
    }

    /** @brief Wie notDriven(), zusätzlich Eingang mit Pull-down wie nach begin(). */
    bool restingInput(uint8_t pin, gpio_io_config_t &config)
    {
        return notDriven(pin, config) && config.ie && config.pd && !config.pu;
    }

    /** @brief Loggt die Konfiguration einer Leitung, die nicht im erwarteten Zustand ist. */
    void logPin(const char *prefix, uint8_t pin, const gpio_io_config_t &config)
    {
        LOG_PLAIN_F("%s G%u Arduino=%d Funktion=%lu Signal=%lu Ausgang=%u Peripherie=%u Eingang=%u PullUp=%u PullDown=%u", prefix, pin, static_cast<int>(perimanGetPinBusType(pin)), static_cast<unsigned long>(config.fun_sel), static_cast<unsigned long>(config.sig_out), config.oe, config.oe_ctrl_by_periph, config.ie, config.pu, config.pd);
    }

    /**
     * @brief Stellt pin als Eingang mit Pull-down ein.
     *
     * gpio_config ruft gpio_output_disable auf. Das löscht im P4 das Freigabebit und stellt die Ausgangsfreigabe auf das GPIO-Register um (OEN_SEL = 1), sodass der Pin auch mit noch verknüpftem UART-Sendesignal nicht treibt (Disassemblierung von libesp_driver_gpio.a, Arduino 3.3.7). esp_rom_gpio_connect_out_signal ist dafür ungeeignet: Es schaltet den Ausgang ein.
     */
    bool makeInput(uint8_t pin)
    {
        gpio_config_t config = {};
        config.pin_bit_mask = 1ULL << pin;
        config.mode = GPIO_MODE_INPUT;
        config.pull_up_en = GPIO_PULLUP_DISABLE;
        config.pull_down_en = GPIO_PULLDOWN_ENABLE;
        config.intr_type = GPIO_INTR_DISABLE;
        return gpio_config(&config) == ESP_OK;
    }

    /** @brief Zusatz für die Logzeile einer Leitung: der beteiligte Pico-Eingang bei vertauscht oder Kurzschluss, sonst leer. */
    const char *partnerText(int index, char (&text)[16])
    {
        text[0] = 0;
        if (index >= 0)
            snprintf(text, sizeof(text), " mit Pico %s", BtProtocol::PicoInputs[index]);
        return text;
    }
}

uint8_t BtSender::releaseAfterReset()
{
    uint8_t driven = 0;
    for (size_t i = 0; i < BtProtocol::IdleCount; ++i)
    {
        gpio_io_config_t config{};
        if (!notDriven(BtProtocol::IdlePins[i], config))
            driven |= static_cast<uint8_t>(1u << i);
        makeInput(BtProtocol::IdlePins[i]);
    }
    return driven;
}

bool BtSender::begin()
{
    bool available = !uart_is_driver_installed(Port);
    if (!available)
        LOG_PLAIN_F("BT LEITUNG UART%d belegt", static_cast<int>(Port));
    for (uint8_t pin : BtProtocol::IdlePins)
    {
        gpio_io_config_t config{};
        if (!notDriven(pin, config))
        {
            logPin("BT LEITUNG belegt", pin, config);
            available = false;
        }
    }
    if (!available)
        return false;
    for (uint8_t pin : BtProtocol::IdlePins)
    {
        gpio_io_config_t config{};
        if (!makeInput(pin) || !restingInput(pin, config))
        {
            logPin("BT LEITUNG kein Eingang", pin, config);
            available = false;
        }
    }
    if (!available)
        return false;
    m_orders = xQueueCreate(OrderSlots, sizeof(Order));
    if (!m_orders)
        return false;
    // Der Stack liegt im PSRAM wie beim FTP-Dienst: Der Task schreibt nie in den Flash.
    if (xTaskCreatePinnedToCoreWithCaps(task, "bt", StackBytes, this, 1, &m_task, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS)
        return true;
    m_task = nullptr;
    return false;
}

BtSender::Start BtSender::startTest()
{
    if (!m_task)
        return Start::NotReady;
    bool idle = false;
    if (!m_running.compare_exchange_strong(idle, true))
        return Start::Busy;
    xTaskNotifyGive(m_task);
    return Start::Started;
}

BtSender::Request BtSender::request(Command command, const char *address)
{
    if (!m_task || !m_orders)
        return Request::NotReady;
    Order order{};
    order.command = command;
    if (command == Command::Connect && (!address || !BtProtocol::parseAddress(address, order.address)))
        return Request::Invalid;
    if (!m_present.load())
        return Request::NoPico;
    if (xQueueSend(m_orders, &order, 0) != pdTRUE)
        return Request::Busy;
    xTaskNotifyGive(m_task);
    return Request::Accepted;
}

void BtSender::status(Status &out) const
{
    taskENTER_CRITICAL(&m_lock);
    out = m_status;
    taskEXIT_CRITICAL(&m_lock);
}

uint32_t BtSender::version() const
{
    return m_version.load();
}

void BtSender::setMuted(bool muted)
{
    // Ohne Pico gibt es nichts zu senden, probe() vergisst den gesendeten Stand beim nächsten Öffnen ohnehin.
    if (m_wantMuted.exchange(muted) != muted && m_present.load())
        xTaskNotifyGive(m_task);
}

void BtSender::setRate(uint32_t rate)
{
    if (rate && m_wantRate.exchange(rate) != rate && m_present.load())
        xTaskNotifyGive(m_task);
}

void BtSender::setVolume(uint8_t volume)
{
    const int wanted = volume > BtProtocol::KnobMax ? BtProtocol::KnobMax : volume;
    if (m_wantVolume.exchange(wanted) != wanted && m_present.load())
        xTaskNotifyGive(m_task);
}

void BtSender::setUkw(bool enabled, uint32_t khz, uint8_t power)
{
    bool changed = m_wantUkwOn.exchange(enabled) != enabled;
    changed = m_wantUkwKhz.exchange(khz) != khz || changed;
    changed = m_wantUkwPower.exchange(power) != power || changed;
    if (changed && m_present.load())
        xTaskNotifyGive(m_task);
}

void BtSender::setRds(const char *name, const char *text)
{
    bool changed = false;
    taskENTER_CRITICAL(&m_lock);
    if (strncmp(m_rdsName, name, sizeof(m_rdsName) - 1) != 0)
    {
        snprintf(m_rdsName, sizeof(m_rdsName), "%s", name);
        ++m_rdsNameVersion;
        changed = true;
    }
    if (strncmp(m_rdsText, text, sizeof(m_rdsText) - 1) != 0)
    {
        snprintf(m_rdsText, sizeof(m_rdsText), "%s", text);
        ++m_rdsTextVersion;
        changed = true;
    }
    taskEXIT_CRITICAL(&m_lock);
    if (changed && m_present.load())
        xTaskNotifyGive(m_task);
}

bool BtSender::routed() const
{
    return m_routed.load();
}

bool BtSender::headphone() const
{
    return m_routed.load() && m_connected.load();
}

bool BtSender::picoAwake() const
{
    // Der Pico sendet ASCII-Zeilen mit High-Stoppbits und Pausen. Acht Ablesungen über 0,8 ms fallen praktisch nie alle auf Null-Bits.
    for (int i = 0; i < AwakeSamples; ++i)
    {
        if (gpio_get_level(static_cast<gpio_num_t>(Tab5Board::BtUartRx)))
            return true;
        delayMicroseconds(AwakeSampleUs);
    }
    return false;
}

void BtSender::task(void *context)
{
    static_cast<BtSender *>(context)->run();
}

void BtSender::run()
{
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, m_uartOpen ? LinkPoll : ProbeWait);
        if (m_running.load())
        {
            if (m_uartOpen)
                closeLink("Leitungstest");
            runTest();
            m_running.store(false);
        }
        else if (m_uartOpen)
            serviceLink();
        else
            probe();
    }
}

bool BtSender::linesResting(const char *prefix)
{
    bool resting = !uart_is_driver_installed(Port);
    for (uint8_t pin : BtProtocol::IdlePins)
    {
        gpio_io_config_t config{};
        if (!restingInput(pin, config))
        {
            if (prefix)
                logPin(prefix, pin, config);
            resting = false;
        }
    }
    return resting;
}

void BtSender::probe()
{
    using namespace BtProtocol;
    if (static_cast<int32_t>(millis() - m_nextProbeMs) < 0)
        return;
    // Steht eine Leitung nicht mehr wie nach begin(), hat sie ein anderer Teil der Firmware übernommen. Dann treibt das Tab5 nichts.
    if (!linesResting(m_quietLogged ? nullptr : "BT LEITUNG verändert"))
    {
        if (!m_quietLogged)
            LOG_PLAIN_F("BT LEITUNG oder UART%d belegt, UART bleibt zu", static_cast<int>(Port));
        m_quietLogged = true;
        return;
    }
    const int tx = idleLevel(Tab5Board::BtUartTx), rx = idleLevel(Tab5Board::BtUartRx);
    const Verdict idle = judgeIdle(tx, rx);
    if (idle != m_lastIdle)
        LOG_PLAIN_F("BT RUHEPEGEL G%u=%d G%u=%d: %s", Tab5Board::BtUartTx, tx, Tab5Board::BtUartRx, rx, idle == Verdict::Ok ? "Pico versorgt" : name(idle));
    m_lastIdle = idle;
    if (idle != Verdict::Ok)
        return;
    char reply[MaxLine + 1]{};
    char stand[StandCapacity]{};
    if (!openUart() || !exchange(Hello, HelloReply, reply) || !parseHello(reply, stand))
    {
        release();
        if (!m_quietLogged)
            LOG_PLAIN_F("BT HALLO ohne Antwort, Leitungen wieder Eingang. Neuer Versuch alle %lu s", static_cast<unsigned long>(HelloRetryMs / 1000));
        m_quietLogged = true;
        m_nextProbeMs = millis() + HelloRetryMs;
        return;
    }
    m_quietLogged = false;
    m_reader = LineReader();
    m_sync.forget();
    m_routeRefused = false;
    m_connected.store(false);
    taskENTER_CRITICAL(&m_lock);
    m_status.present = true;
    memcpy(m_status.stand, stand, sizeof(stand));
    m_status.link = LinkView();
    m_version.store(++m_status.version);
    taskEXIT_CRITICAL(&m_lock);
    m_present.store(true);
    LOG_PLAIN_F("BT PICO verbunden, Stand=%s", stand);
    m_nextPresenceMs = millis() + PresenceIntervalMs;
    m_lowChecks = 0;
    sendLine(Query);
    // Ein Pico vor Stand 5.3 antwortet FEHLER UNBEKANNT, die UKW-Seite zeigt dann „nicht angeschlossen“.
    m_ukwSync.forget();
    sendLine(Ukw);
}

void BtSender::serviceLink()
{
    Order order;
    while (xQueueReceive(m_orders, &order, 0) == pdTRUE)
    {
        char line[BtProtocol::MaxLine + 1];
        const char *command = CommandLines[static_cast<size_t>(order.command)];
        if (order.command == Command::Connect)
            snprintf(line, sizeof(line), "%s %s", command, order.address);
        else
            snprintf(line, sizeof(line), "%s", command);
        taskENTER_CRITICAL(&m_lock);
        m_status.link.error[0] = 0;
        m_version.store(++m_status.version);
        taskEXIT_CRITICAL(&m_lock);
        sendLine(line);
    }
    size_t buffered = 0;
    uint8_t bytes[64];
    while (uart_get_buffered_data_len(Port, &buffered) == ESP_OK && buffered > 0)
    {
        const int got = uart_read_bytes(Port, bytes, std::min(buffered, sizeof(bytes)), 0);
        if (got <= 0)
            break;
        for (int i = 0; i < got; ++i)
            if (m_reader.push(static_cast<char>(bytes[i])))
                handleLine(m_reader.line());
    }
    updateRoute();
    syncOutput();
    syncUkw();
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - m_nextPresenceMs) < 0)
        return;
    m_nextPresenceMs = now + PresenceIntervalMs;
    // Ohne laufenden Pico liegt G7 dauerhaft Low. Während einer Übertragung können acht Ablesungen zufällig alle auf Null-Bits fallen (unter 1 %), zweimal hintereinander praktisch nie.
    m_lowChecks = idleLevel(Tab5Board::BtUartRx) == 0 ? m_lowChecks + 1 : 0;
    if (m_lowChecks >= LowChecksToClose)
        closeLink("G7 in Ruhe Low, Pico ohne Versorgung oder im Reset");
}

void BtSender::sendLine(const char *line)
{
    uart_write_bytes(Port, line, strlen(line));
    uart_write_bytes(Port, "\n", 1);
    LOG_PLAIN_F("BT -> %s", line);
}

void BtSender::handleLine(const char *line)
{
    BtProtocol::Message message;
    if (!BtProtocol::parseMessage(line, message))
    {
        LOG_PLAIN_F("BT PICO unbekannt: %s", line);
        return;
    }
    // Eine Meldung, die vor der Bestätigung eines gesendeten Werts ankommt, beschreibt den Kopfhörer von vorher.
    if (message.kind == BtProtocol::Reply::HeadphoneLevel && m_sync.awaitingVolume())
    {
        LOG_PLAIN_F("BT PICO %s (veraltet, uebergangen)", line);
        return;
    }
    LOG_PLAIN_F("BT PICO %s", line);
    if (message.kind == BtProtocol::Reply::VolumeDone)
        m_sync.volumeAcked();
    const bool wasConnected = m_status.link.state == BtProtocol::LinkState::Connected, ukwWasPresent = m_status.link.ukw.present();
    taskENTER_CRITICAL(&m_lock);
    if (message.kind == BtProtocol::Reply::Hello)
        memcpy(m_status.stand, message.text, sizeof(m_status.stand));
    const bool confirm = BtProtocol::apply(m_status.link, message);
    m_version.store(++m_status.version);
    taskEXIT_CRITICAL(&m_lock);
    const bool connected = m_status.link.state == BtProtocol::LinkState::Connected;
    m_connected.store(connected);
    // Der Pico beginnt jede Verbindung stumm, Stumm- und Ratenstand gehen danach erneut hinaus.
    if (!wasConnected && connected)
        m_sync.forget();
    // Ein neu erkannter Modulator beginnt mit den Vorgaben des Pico. Frequenz, Leistung, Texte und Schalter gehen erneut hinaus.
    if (!ukwWasPresent && m_status.link.ukw.present())
        m_ukwSync.forget();
    // Der Drehknopf übernimmt erst die Meldung des Kopfhörers, ein Wunsch von vorher würde dessen gespeicherte Lautstärke überschreiben.
    if (connected != wasConnected)
        m_wantVolume.store(-1);
    // Der Wunsch folgt der Meldung sofort. Sonst schickte syncOutput() den alten Knopfwert, bevor der UI-Task die Meldung übernimmt.
    if (message.kind == BtProtocol::Reply::HeadphoneLevel)
    {
        m_sync.noteReport(message.level);
        m_wantVolume.store(BtProtocol::fromHeadphone(message.level));
    }
    if (confirm)
        sendLine(BtProtocol::Query);
}

void BtSender::updateRoute()
{
    if (!BtProtocol::routeAudio(m_uartOpen))
    {
        m_routeRefused = false;
        if (m_routed.load())
            unrouteI2s("UART zu");
        return;
    }
    if (!m_routed.load() && !m_routeRefused)
        m_routeRefused = !routeI2s();
}

bool BtSender::routeI2s()
{
    using namespace BtProtocol;
    for (size_t i = 0; i < LineCount; ++i)
    {
        gpio_io_config_t source{};
        if (gpio_get_io_config(static_cast<gpio_num_t>(TapSources[i]), &source) != ESP_OK || !source.oe || source.sig_out != tapSignal(i))
        {
            logPin("BT TON nicht angelegt, Codec-Pin ohne I2S0-Signal:", TapSources[i], source);
            return false;
        }
    }
    // Wie der I2S-Treiber an den Codec-Pins: Ausgang einschalten, dann das Peripheriesignal auf den Pin legen. Die schwächste Treiberstufe reicht für den Bittakt (bis 3,1 MHz bei 48 kHz, 6,1 MHz bei 96 kHz) über rund 20 cm und dämpft Überschwingen und Übersprechen zwischen den ungeschirmten Dupont-Kabeln ohne Serienwiderstand (nicht gemessen).
    for (size_t i = 0; i < LineCount; ++i)
    {
        const gpio_num_t pin = static_cast<gpio_num_t>(LinePins[i]);
        gpio_set_level(pin, 0);
        gpio_set_drive_capability(pin, WeakDrive);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        esp_rom_gpio_connect_out_signal(pin, tapSignal(i), false, false);
    }
    bool routed = true;
    for (size_t i = 0; i < LineCount; ++i)
    {
        gpio_io_config_t config{};
        if (gpio_get_io_config(static_cast<gpio_num_t>(LinePins[i]), &config) != ESP_OK || !config.oe || config.sig_out != tapSignal(i) || config.drv != WeakDrive)
        {
            logPin("BT TON Leitung nicht wie verlangt:", LinePins[i], config);
            routed = false;
        }
    }
    if (!routed)
    {
        for (uint8_t pin : LinePins)
            makeInput(pin);
        LOG_PLAIN_F("BT TON nicht angelegt, G%u/G%u/G%u wieder Eingang", LinePins[0], LinePins[1], LinePins[2]);
        return false;
    }
    m_routed.store(true);
    LOG_PLAIN_F("BT TON an: I2S0 BCLK/WS/DATEN zusaetzlich an G%u/G%u/G%u", LinePins[0], LinePins[1], LinePins[2]);
    return true;
}

void BtSender::unrouteI2s(const char *reason)
{
    for (uint8_t pin : BtProtocol::LinePins)
        makeInput(pin);
    m_routed.store(false);
    m_wantVolume.store(-1);
    LOG_PLAIN_F("BT TON aus (%s), G%u/G%u/G%u wieder Eingang", reason, BtProtocol::LinePins[0], BtProtocol::LinePins[1], BtProtocol::LinePins[2]);
}

void BtSender::syncOutput()
{
    char line[BtProtocol::MaxLine + 1];
    while (m_sync.next(m_wantMuted.load(), m_wantRate.load(), m_connected.load() ? m_wantVolume.load() : -1, line))
        sendLine(line);
}

void BtSender::syncUkw()
{
    if (!m_status.link.ukw.present())
        return;
    BtProtocol::UkwWish wish;
    char name[BtProtocol::RdsTextBytes], text[BtProtocol::RdsTextBytes];
    taskENTER_CRITICAL(&m_lock);
    memcpy(name, m_rdsName, sizeof(name));
    memcpy(text, m_rdsText, sizeof(text));
    wish.psVersion = m_rdsNameVersion;
    wish.rtVersion = m_rdsTextVersion;
    taskEXIT_CRITICAL(&m_lock);
    wish.enabled = m_wantUkwOn.load();
    wish.khz = m_wantUkwKhz.load();
    wish.power = m_wantUkwPower.load();
    wish.ps = name;
    wish.rt = text;
    char line[BtProtocol::MaxLine + 1];
    while (m_ukwSync.next(wish, true, line))
        sendLine(line);
}

void BtSender::closeLink(const char *reason)
{
    release();
    xQueueReset(m_orders);
    m_present.store(false);
    m_connected.store(false);
    m_lastIdle = BtProtocol::Verdict::Unchecked;
    taskENTER_CRITICAL(&m_lock);
    m_status.present = false;
    m_status.stand[0] = 0;
    m_status.link = BtProtocol::LinkView();
    m_version.store(++m_status.version);
    taskEXIT_CRITICAL(&m_lock);
    LOG_PLAIN_F("BT PICO getrennt: %s, Leitungen wieder Eingang", reason);
}

void BtSender::runTest()
{
    using namespace BtProtocol;
    const uint32_t start = millis();
    LOG_PLAIN_F("BT TEST START");
    TestResult result;
    if (!linesResting("BT TEST LEITUNG verändert"))
        LOG_PLAIN_F("BT TEST ABBRUCH: Leitung oder UART%d seit dem Start belegt, nichts geschaltet", static_cast<int>(Port));
    else
    {
        result = runLineTest(*this);
        release();
        LOG_PLAIN_F("BT TEST RUHEPEGEL G%u=%d G%u=%d G%u=%d G%u=%d G%u=%d", IdlePins[0], result.levels[0], IdlePins[1], result.levels[1], IdlePins[2], result.levels[2], IdlePins[3], result.levels[3], IdlePins[4], result.levels[4]);
        if (!result.powered)
        {
            if (result.uart == Verdict::Missing)
                LOG_PLAIN_F("BT TEST KEIN PICO: G7 in Ruhe Low, Pico nicht versorgt, ohne Firmware oder Bus 15 offen. Nichts geschaltet");
            else if (result.uart == Verdict::Swapped)
                LOG_PLAIN_F("BT TEST UART VERTAUSCHT: G6 in Ruhe High, G7 Low (Bus 15 und 16 getauscht). Nichts geschaltet");
            else if (result.uart == Verdict::Short)
                LOG_PLAIN_F("BT TEST UART KURZSCHLUSS: G6 und G7 in Ruhe High. Nichts geschaltet");
            else
                LOG_PLAIN_F("BT TEST UART GESTOERT: Ruhepegel an G6 oder G7 wechselt. Nichts geschaltet");
        }
        else if (result.uart == Verdict::Unchecked)
            LOG_PLAIN_F("BT TEST UART%d nicht gestartet", static_cast<int>(Port));
        else if (result.uart == Verdict::Missing)
            LOG_PLAIN_F("BT TEST HALLO ohne Antwort: Bus 16 (G6 -> Pico G25) offen oder Pico ohne Protokoll");
        else
            LOG_PLAIN_F("BT TEST HALLO STAND=%s", result.stand);
        for (size_t i = 0; i < LineCount; ++i)
        {
            if (result.lines[i] == Verdict::Unchecked)
                continue;
            if (result.levels[2 + i] != 0)
                LOG_PLAIN_F("BT TEST %s G%u in Ruhe %s: %s, nicht geschaltet", LineNames[i], LinePins[i], result.levels[2 + i] > 0 ? "High" : "wechselnd", name(result.lines[i]));
            else
            {
                char partner[16];
                LOG_PLAIN_F("BT TEST %s G%u -> Pico %s=%lu %s=%lu %s=%lu: %s%s", LineNames[i], LinePins[i], PicoInputs[0], static_cast<unsigned long>(result.counts[i][0]), PicoInputs[1], static_cast<unsigned long>(result.counts[i][1]), PicoInputs[2], static_cast<unsigned long>(result.counts[i][2]), name(result.lines[i]), partnerText(result.partner[i], partner));
            }
        }
        if (result.uart == Verdict::Disturbed && result.powered)
            LOG_PLAIN_F("BT TEST ABBRUCH: Antwort des Pico fehlt oder ist ungültig");
    }
    LOG_PLAIN_F("BT TEST UART=%s %s=%s %s=%s %s=%s", name(result.uart), LineNames[0], name(result.lines[0]), LineNames[1], name(result.lines[1]), LineNames[2], name(result.lines[2]));
    bool inputs = true;
    for (uint8_t pin : IdlePins)
    {
        gpio_io_config_t config{};
        if (!restingInput(pin, config))
        {
            logPin("BT TEST LEITUNG nicht in Ruhe", pin, config);
            inputs = false;
        }
    }
    LOG_PLAIN_F("BT TEST ENDE Dauer=%lu ms Leitungen=%s Stack=%u", static_cast<unsigned long>(millis() - start), inputs ? "Eingang" : "FEHLER", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

int BtSender::idleLevel(uint8_t pin)
{
    const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
    const int first = gpio_get_level(gpio);
    for (int i = 1; i < IdleSamples; ++i)
    {
        vTaskDelay(1);
        if (gpio_get_level(gpio) != first)
            return -1;
    }
    return first;
}

bool BtSender::openUart()
{
    uart_config_t config = {};
    config.baud_rate = static_cast<int>(BtProtocol::Baud);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(Port, RxBufferBytes, 0, 0, nullptr, 0) != ESP_OK)
        return false;
    m_uartOpen = true;
    const gpio_num_t tx = static_cast<gpio_num_t>(Tab5Board::BtUartTx), rx = static_cast<gpio_num_t>(Tab5Board::BtUartRx);
    if (uart_param_config(Port, &config) != ESP_OK || gpio_set_drive_capability(tx, WeakDrive) != ESP_OK || uart_set_pin(Port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
        return false;
    // uart_set_pin kann Pull und Treiberstufe neu setzen. Pull-down wie in Ruhe, damit eine offene Leitung als Low statt als Rauschen ankommt.
    gpio_set_pull_mode(rx, GPIO_PULLDOWN_ONLY);
    gpio_set_drive_capability(tx, WeakDrive);
    vTaskDelay(UartSettle);
    // Ein einzelnes Zeilenende schließt beim Pico eine Zeile aus der Low-Phase ab, ohne Antwort.
    uart_write_bytes(Port, "\n", 1);
    return true;
}

bool BtSender::exchange(const char *command, const char *expected, char (&reply)[BtProtocol::MaxLine + 1])
{
    uart_flush_input(Port);
    uart_write_bytes(Port, command, strlen(command));
    uart_write_bytes(Port, "\n", 1);
    BtProtocol::LineReader reader;
    const size_t prefix = strlen(expected);
    const TickType_t begun = xTaskGetTickCount(), timeout = pdMS_TO_TICKS(BtProtocol::ReplyTimeoutMs);
    for (;;)
    {
        const TickType_t elapsed = xTaskGetTickCount() - begun;
        uint8_t byte = 0;
        if (elapsed >= timeout || uart_read_bytes(Port, &byte, 1, timeout - elapsed) != 1)
            return false;
        if (reader.push(static_cast<char>(byte)) && strncmp(reader.line(), expected, prefix) == 0)
        {
            snprintf(reply, sizeof(reply), "%s", reader.line());
            return true;
        }
    }
}

void BtSender::toggle(uint8_t pin, uint16_t edges)
{
    const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
    gpio_set_level(gpio, 0);
    gpio_set_drive_capability(gpio, WeakDrive);
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    for (uint16_t i = 0; i < edges; ++i)
    {
        vTaskDelay(HalfPeriod);
        gpio_set_level(gpio, (i + 1) & 1);
    }
    vTaskDelay(HalfPeriod);
    makeInput(pin);
}

void BtSender::release()
{
    if (m_uartOpen)
    {
        uart_driver_delete(Port);
        m_uartOpen = false;
    }
    for (uint8_t pin : BtProtocol::IdlePins)
        makeInput(pin);
    m_wantVolume.store(-1);
    if (m_routed.exchange(false))
        LOG_PLAIN_F("BT TON aus, alle Leitungen wieder Eingang");
}
