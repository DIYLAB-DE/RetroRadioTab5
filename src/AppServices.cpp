#include "AppServices.h"
#include "FatDirectory.h"
#include "Tab5Config.h"
#include "StringUtils.h"
#include "Logger.h"
#include <WiFi.h>
#include <Network.h>
#include <SD_MMC.h>
#include <sdmmc_cmd.h>
#include <ff.h>
#include <diskio_sdmmc.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <nvs.h>
#include <mbedtls/platform_util.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <new>
#include <sys/time.h>

std::atomic<uint32_t> AppServices::s_connections{0};
std::atomic<bool> AppServices::s_ntpSynced{false};
std::atomic<bool> AppServices::s_rtcSyncPending{false};

namespace
{
    /** @brief Liest die Kartenbeschreibung, die SDMMCFS nur abgeleiteten Klassen zugänglich macht. Der Zeiger auf das geschützte Element braucht keine eigene Instanz. */
    struct CardAccess : fs::SDMMCFS
    {
        /** @brief Liefert die Karte von SD_MMC, nullptr ohne eingebundene Karte. */
        static sdmmc_card_t *card()
        {
            return SD_MMC.*&CardAccess::_card;
        }
    };

    /** @brief Meldet, ob der Audioworker eine Datei der Karte offen haben kann. Ein noch nicht bearbeiteter Auftrag meldet getStatus() als Starting und zählt mit, weil er eine Datei öffnen kann. */
    bool audioUsesCard()
    {
        const auto audio = g_audioManager.getStatus();
        return audio.state == AudioManager::State::Starting || (audio.file && (audio.state == AudioManager::State::Streaming || audio.state == AudioManager::State::Paused));
    }

    /** @brief Liest Kartenordner für SdFileScan wie Portal und FTP über FATFS (FatDirectory), damit die FAT-Attribute versteckt und System für SystemEntries ankommen. Nur im Dienstworker. Vor jedem Eintrag wird 1 ms abgegeben, damit gleich priorisierte Tasks laufen. */
    class CardDirectory : public SdFileScan::Directory
    {
    public:
        /** @brief Bindet den Abbruch an einen Auftrag. @param cancel Kennung des abzubrechenden Auftrags. @param request Kennung dieses Auftrags. @param drive FATFS-Laufwerk der eingebundenen Karte, -1 ohne Karte. */
        CardDirectory(const std::atomic<uint32_t> &cancel, uint32_t request, int drive) : m_cancel(cancel), m_request(request)
        {
            m_directory.setDrive(drive);
        }

        bool open(const char *path) override
        {
            return m_directory.open(path) == FtpProtocol::Status::Ok;
        }

        bool next(Item &item) override
        {
            vTaskDelay(pdMS_TO_TICKS(1));
            bool hidden = false;
            if (m_directory.next(m_entry, hidden) != FtpProtocol::Status::Ok)
                return false;
            item.name = m_entry.name;
            item.directory = m_entry.directory;
            item.size = static_cast<uint32_t>(m_entry.size);
            item.hidden = hidden;
            return true;
        }

        void close() override
        {
            m_directory.close();
        }

        bool cancelled() override
        {
            return m_cancel.load() == m_request;
        }

    private:
        const std::atomic<uint32_t> &m_cancel; ///< Kennung des abzubrechenden Auftrags
        uint32_t m_request;                    ///< Kennung dieses Auftrags
        FatDirectory m_directory;              ///< Ordnerleser über FATFS
        FtpProtocol::Entry m_entry;            ///< Zuletzt gelesener Eintrag, dessen Name item.name trägt
    };
}

bool AppServices::cardAnswers()
{
    sdmmc_card_t *card = CardAccess::card();
    for (int attempt = 0; card && attempt < 3; ++attempt)
        if (sdmmc_get_status(card) == ESP_OK)
            return true;
    return false;
}

int AppServices::cardDrive()
{
    const sdmmc_card_t *card = CardAccess::card();
    if (!card)
        return -1;
    const BYTE drive = ff_diskio_get_pdrv_card(card);
    return drive < FF_VOLUMES ? drive : -1;
}

bool AppServices::validConfiguration(const Settings::Configuration &config)
{
    return Tab5Config::valid(config);
}

void AppServices::load()
{
    Tab5Config::load(m_config, m_revision, &m_extras);
    m_radio = Tab5Config::loadRadio();
    // Die Warnung zu einem ungültigen Eintrag meldet schon das Laden beim Start in setup().
    m_modernStored = Tab5Config::loadModern(m_modern, false);
    if (Tab5Config::loadFtpPassword(m_ftpPassword))
        m_ftpPasswordRevision.store(1);
}

Settings::Sound AppServices::sound()
{
    portENTER_CRITICAL(&m_configLock);
    const auto value = m_extras.sound;
    portEXIT_CRITICAL(&m_configLock);
    return value;
}

bool AppServices::validatePin(const char *pin)
{
    if (!pin || strnlen(pin, 5) != 4)
        return false;
    unsigned difference = 0;
    portENTER_CRITICAL(&m_configLock);
    for (size_t i = 0; i < 4; ++i)
        difference |= static_cast<unsigned char>(pin[i]) ^ static_cast<unsigned char>(m_extras.pin[i]);
    portEXIT_CRITICAL(&m_configLock);
    return difference == 0;
}

bool AppServices::pinLockEnabled()
{
    portENTER_CRITICAL(&m_configLock);
    const bool enabled = m_config.pinLock;
    portEXIT_CRITICAL(&m_configLock);
    return enabled;
}

uint64_t AppServices::configuration(Settings::Configuration &config)
{
    portENTER_CRITICAL(&m_configLock);
    config = m_config;
    const auto revision = m_revision;
    portEXIT_CRITICAL(&m_configLock);
    return revision;
}

bool AppServices::begin(uint8_t defaultStation)
{
    // Netzwerkstack vor dem Worker vollständig starten: HTTP darf nicht mit der WLAN-Initialisierung konkurrieren.
    if (!Network.begin())
    {
        LOG_ERROR("Netzwerkstack konnte nicht gestartet werden.");
        return false;
    }
    m_webFiles = xQueueCreate(1, sizeof(WebFileRequest));
    m_webResults = xQueueCreate(1, sizeof(WebFileResult));
    m_webEntries = static_cast<SdFileEntry *>(heap_caps_calloc(FileExplorer::MaxEntries, sizeof(SdFileEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (void *memory = heap_caps_calloc(1, sizeof(FatDirectory), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
        m_webDirectory = new (memory) FatDirectory();
    if (void *memory = m_webDirectory ? heap_caps_calloc(1, sizeof(WebFileStore), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr)
        m_webStore = new (memory) WebFileStore("/sdcard", m_webDirectory, esp_random);
    m_coverRequests = xQueueCreate(1, sizeof(CoverRequest));
    m_coverBytes = static_cast<uint8_t *>(heap_caps_malloc(1000000, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    m_coverPixels = static_cast<uint16_t *>(heap_caps_malloc(SdPlayer::CoverWidth * SdPlayer::CoverHeight * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    m_coverCanvas.setPsram(true);
    m_coverCanvas.setColorDepth(16);
    const bool coverReady = m_coverRequests && m_coverBytes && m_coverPixels && m_coverCanvas.createSprite(SdPlayer::CoverWidth, SdPlayer::CoverHeight);
    load();
    m_radioNvs = m_radio;
    m_radio.station = defaultStation < 70 ? defaultStation : 0;
    const bool rtcReady = m_rtc.begin();
    // Auch ohne RTC aufrufen: loadRadio begrenzt dann den Pegel aus radio_v1.
    if (m_rtc.loadRadio(m_radio, AudioManager::DefaultVolume))
        LOG_INFO("RTC Radiozustand geladen.");
    else
        LOG_INFO_F("RTC Radiozustand fehlt oder ist ungültig, NVS-Werte mit Pegel höchstens %u übernommen.", AudioManager::DefaultVolume);
    if (rtcReady)
    {
        const auto status = m_rtc.status();
        LOG_INFO_F("RTC RX8130CE verfügbar, UTC=%lld Bootübernahme=%u Flags=0x%02x", static_cast<long long>(status.utc), status.restored, status.flags);
    }
    else
        LOG_WARNING("RTC nicht verfügbar. Systemzeit braucht NTP/manuelles Stellen, Radiowerte nutzen NVS-Rückfall.");
    m_rtcStatus = m_rtc.status();
    m_radioGeneration = m_rtcStatus.available ? 1 : 0;
    m_requests = xQueueCreate(3, sizeof(Settings::Request));
    m_results = xQueueCreate(3, sizeof(Result));
    m_files = xQueueCreate(1, sizeof(SdFileRequest));
    m_modernRequests = xQueueCreate(1, sizeof(ModernRequest));
    m_modernResults = xQueueCreate(1, sizeof(ModernResult));
    m_entries = static_cast<SdFileEntry *>(heap_caps_calloc(FileExplorer::MaxEntries, sizeof(SdFileEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    m_pending = static_cast<SdFileScan::Pending *>(heap_caps_malloc(SdFileScan::MaxPending * sizeof(SdFileScan::Pending), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (coverReady && m_requests && m_results && m_files && m_modernRequests && m_modernResults && m_entries && m_pending && m_webFiles && m_webResults && m_webEntries && m_webStore && beginFtp() && xTaskCreatePinnedToCore(worker, "app-services", 12288, this, 1, &m_task, 0) == pdPASS)
        return true;
    if (m_webFiles)
        vQueueDelete(m_webFiles);
    if (m_webResults)
        vQueueDelete(m_webResults);
    heap_caps_free(m_webEntries);
    m_webFiles = m_webResults = nullptr;
    m_webEntries = nullptr;
    if (m_webStore)
    {
        m_webStore->~WebFileStore();
        heap_caps_free(m_webStore);
        m_webStore = nullptr;
    }
    if (m_webDirectory)
    {
        m_webDirectory->~FatDirectory();
        heap_caps_free(m_webDirectory);
        m_webDirectory = nullptr;
    }
    if (m_requests)
        vQueueDelete(m_requests);
    if (m_results)
        vQueueDelete(m_results);
    if (m_files)
        vQueueDelete(m_files);
    if (m_modernRequests)
        vQueueDelete(m_modernRequests);
    if (m_modernResults)
        vQueueDelete(m_modernResults);
    m_modernRequests = m_modernResults = nullptr;
    if (m_coverRequests)
        vQueueDelete(m_coverRequests);
    m_coverRequests = nullptr;
    heap_caps_free(m_coverBytes);
    heap_caps_free(m_coverPixels);
    m_coverBytes = nullptr;
    m_coverPixels = nullptr;
    m_coverCanvas.deleteSprite();
    heap_caps_free(m_entries);
    m_entries = nullptr;
    heap_caps_free(m_pending);
    m_pending = nullptr;
    m_requests = m_results = m_files = nullptr;
    endFtp();
    return false;
}

bool AppServices::request(const Settings::Request &request)
{
    if (!m_requests)
        return false;
    if (request.type == Settings::RequestType::CancelScan || request.type == Settings::RequestType::CancelTest)
    {
        m_cancel.store(request.targetId);
        return true;
    }
    return xQueueSend(m_requests, &request, 0) == pdPASS;
}

bool AppServices::takeResult(Result &result)
{
    return m_results && xQueueReceive(m_results, &result, 0) == pdPASS;
}

bool AppServices::temporaryWifi(const char *ssid, const char *password)
{
    Settings::Request r;
    r.type = Settings::RequestType::TestProfile;
    r.id = UINT32_MAX;
    if (!ssid || !password || strlen(ssid) > 32 || strlen(password) > 64)
        return false;
    stringutils::safeCopy(r.profile.ssid, ssid);
    stringutils::safeCopy(r.profile.password, password);
    const bool ok = request(r);
    mbedtls_platform_zeroize(&r, sizeof(r));
    return ok;
}

AppServices::NetworkStatus AppServices::networkStatus()
{
    portENTER_CRITICAL(&m_configLock);
    const auto status = m_network;
    portEXIT_CRITICAL(&m_configLock);
    return status;
}

void AppServices::updateNetwork()
{
    if (!m_wifiReady || millis() - m_lastNetwork < 1000)
        return;
    m_lastNetwork = millis();
    NetworkStatus status;
    wifi_ap_record_t ap{};
    status.connected = WiFi.status() == WL_CONNECTED && esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    if (status.connected)
    {
        memcpy(status.ssid, ap.ssid, 32);
        status.rssi = ap.rssi;
        const auto ip = WiFi.localIP(), gateway = WiFi.gatewayIP();
        status.channel = ap.primary;
        for (int i = 0; i < 4; ++i)
        {
            status.ip[i] = ip[i];
            status.gateway[i] = gateway[i];
        }
    }
    uint8_t mac[6]{};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK)
        snprintf(status.mac, sizeof(status.mac), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    portENTER_CRITICAL(&m_configLock);
    m_network = status;
    portEXIT_CRITICAL(&m_configLock);
}

bool AppServices::prepareWifi()
{
    if (m_wifiReady)
        return true;
    if (!WiFi.setPins(12, 13, 11, 10, 9, 8, 15))
    {
        LOG_ERROR("WLAN Pins nicht gesetzt.");
        return false;
    }
    WiFi.persistent(false);
    WiFi.setHostname(m_config.hostname);
    LOG_INFO("WLAN: C6-Station initialisieren.");
    m_wifiReady = WiFi.STA.begin(false);
    if (!m_wifiReady)
    {
        LOG_PLAIN_F("WIFI_PROFILE ERROR_INIT");
        return false;
    }
    uint8_t mac[6]{};
    const auto result = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (result != ESP_OK || (mac[0] & 1) || !(mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]))
    {
        LOG_PLAIN_F("WIFI_PROFILE ERROR_MAC");
        m_wifiReady = false;
        return false;
    }
    return true;
}

void AppServices::applyTime()
{
    setenv("TZ", m_config.timezonePosix, 1);
    tzset();
    if (WiFi.status() == WL_CONNECTED)
    {
        sntp_set_time_sync_notification_cb([](struct timeval *)
        {
            s_ntpSynced.store(true);
            s_rtcSyncPending.store(true);
        });
        configTzTime(m_config.timezonePosix, m_config.ntpServer);
        sntp_set_sync_interval(m_config.syncIntervalMs);
    }
    m_timeRevision.fetch_add(1);
}

void AppServices::connectProfiles()
{
    static_assert(Settings::ProfileCount == WifiSequence::MaxProfiles, "Profilplätze von Einstellungen und Verbindungsablauf");
    if (!m_wifiReady)
        return;
    const char *ssids[WifiSequence::MaxProfiles]{};
    for (size_t i = 0; i < WifiSequence::MaxProfiles; ++i)
        if (m_config.profiles[i].enabled && m_config.profiles[i].ssid[0])
            ssids[i] = m_config.profiles[i].ssid;
    // Erst nach wartenden Aufträgen zurückkehren, damit ein beim Verlassen noch offenes Speichern berücksichtigt ist.
    if (!uxQueueMessagesWaiting(m_requests) && m_restoreWifi.exchange(false))
        m_sequence.restore(ssids);
    const auto step = m_sequence.poll(millis(), s_connections.load(), ssids);
    if (step == WifiSequence::Step::Connect)
    {
        const auto &profile = m_config.profiles[m_sequence.profile()];
        WiFi.disconnect(false, false);
        const bool started = WiFi.STA.connect(profile.ssid, profile.password);
        LOG_INFO_F("WLAN Profil %u: Verbindungsversuch%s", static_cast<unsigned>(m_sequence.profile() + 1), started ? "" : " nicht gestartet");
    }
    else if (step == WifiSequence::Step::Connected)
    {
        LOG_INFO_F("WLAN verbunden IP=%s RSSI=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        applyTime();
    }
    m_wifiConnecting.store(m_sequence.active());
}

void AppServices::execute()
{
    using Type = Settings::RequestType;
    using Error = Settings::Error;
    m_result = {};
    m_result.type = m_work.type;
    m_result.id = m_work.id;
    m_result.error = Error::None;
    m_result.outcome = Settings::Outcome::Success;
    if (m_work.type == Type::Save || m_work.type == Type::ResetDefaults)
    {
        m_result.changedFields = m_work.type == Type::Save ? m_work.changedFields : UINT32_MAX;
        Settings::Configuration candidate = m_work.configuration;
        Tab5Config::Extras extras = m_extras;
        if (m_work.type == Type::ResetDefaults)
            Tab5Config::defaults(candidate, extras);
        else
        {
            if (m_work.changedFields & Settings::Pin)
                memcpy(extras.pin, m_work.pin, sizeof(extras.pin));
            if (m_work.changedFields & Settings::SoundField)
                extras.sound = m_work.sound;
            extras.sound.playing = false;
        }
        if (m_work.baselineRevision != m_revision)
        {
            m_result.error = Error::Conflict;
        }
        else if ((m_work.id & 0x80000000u) && (m_work.changedFields & Settings::Pin) && !validatePin(m_work.currentPin))
        {
            m_result.error = Error::Authentication;
        }
        // Ausschalten verlangt wie im Original an Display und Webportal die aktuelle PIN.
        else if ((m_work.changedFields & Settings::PinLock) && m_config.pinLock && !candidate.pinLock && !validatePin(m_work.currentPin))
        {
            m_result.error = Error::Authentication;
        }
        else if (!validConfiguration(candidate) || !Tab5Config::valid(extras))
        {
            m_result.error = Error::Invalid;
        }
        // Log-Schalter und PIN-Sperre schreiben wie im Original nur ihren eigenen Eintrag, der Sekundenzeiger ebenso. Alle anderen Aufträge schreiben den ganzen Datensatz.
        else if (m_result.changedFields == Settings::SerialLogs   ? !Tab5Config::saveSerialLogs(candidate.serialLogs)
                 : m_result.changedFields == Settings::PinLock    ? !Tab5Config::savePinLock(candidate.pinLock)
                 : m_result.changedFields == Settings::SecondHand ? !Tab5Config::saveSecondHand(candidate.secondSweep)
                                                                  : !Tab5Config::save(candidate, m_revision + 1, extras))
        {
            m_result.error = Error::Storage;
        }
        else
        {
            const bool timeChanged = strcmp(m_config.ntpServer, candidate.ntpServer) || strcmp(m_config.timezonePosix, candidate.timezonePosix) || m_config.syncIntervalMs != candidate.syncIntervalMs;
            portENTER_CRITICAL(&m_configLock);
            m_config = candidate;
            m_extras = extras;
            ++m_revision;
            portEXIT_CRITICAL(&m_configLock);
            if (timeChanged)
                applyTime();
        }
        m_result.revision = configuration(m_result.config);
        mbedtls_platform_zeroize(&candidate, sizeof(candidate));
        mbedtls_platform_zeroize(&extras, sizeof(extras));
    }
    else if (m_work.type == Type::SetTime)
    {
        if (!Settings::validDate(m_work.localTime) || m_work.second > 59)
            m_result.error = Error::Invalid;
        else
        {
            tm local{};
            local.tm_year = m_work.localTime.year - 1900;
            local.tm_mon = m_work.localTime.month - 1;
            local.tm_mday = m_work.localTime.day;
            local.tm_hour = m_work.localTime.hour;
            local.tm_min = m_work.localTime.minute;
            local.tm_sec = m_work.second;
            local.tm_isdst = -1;
            time_t value = mktime(&local);
            if (value < 0 || local.tm_year + 1900 != m_work.localTime.year || local.tm_mon + 1 != m_work.localTime.month || local.tm_hour != m_work.localTime.hour || local.tm_min != m_work.localTime.minute || local.tm_mday != m_work.localTime.day)
                m_result.error = Error::Invalid;
            else
            {
                timeval tv{value, 0};
                if (settimeofday(&tv, nullptr) != 0)
                    m_result.error = Error::Service;
                else if (!m_rtc.syncFromSystem())
                {
                    m_result.error = Error::Storage;
                    s_rtcSyncPending.store(true);
                    LOG_ERROR("Manuelle Systemzeit gesetzt, RTC-Speicherung nicht bestätigt.");
                }
                else
                    LOG_INFO("Manuelle Zeit als UTC in RTC bestätigt.");
            }
        }
    }
    else if (m_work.type == Type::Scan || m_work.type == Type::TestProfile)
    {
        m_wifiBusy.store(true);
        if (!prepareWifi())
            m_result.error = Error::Service;
        else if (m_work.type == Type::Scan)
        {
            const auto scanStart = millis();
            LOG_INFO("WLAN_SCAN START");
            const bool reconnect = WiFi.getAutoReconnect();
            WiFi.setAutoReconnect(false);
            // Der C6 kann nach einem P4-Neustart noch einen früheren Verbindungsversuch fortsetzen.
            // esp_wifi_scan_start lehnt währenddessen mit WIFI_STATE ab. Nur die Verbindung beenden, keine NVS-Daten löschen.
            const bool connected = WiFi.status() == WL_CONNECTED;
            if (!connected)
                WiFi.disconnect(false, false);
            WiFi.setScanTimeout(15000);
            const int count = WiFi.scanNetworks(false, false, false, 150);
            LOG_INFO_F("WLAN_SCAN Ergebnis=%d Dauer=%lu ms Status=%d", count, static_cast<unsigned long>(millis() - scanStart), WiFi.status());
            if (count < 0)
                m_result.error = Error::Service;
            else
                for (int i = 0; i < count; ++i)
                {
                    const String name = WiFi.SSID(i);
                    if (name.isEmpty() || name.length() > 32 || !SettingsUi::validText(name.c_str(), 33))
                        continue;
                    if (m_result.count == WifiList::MaxNetworks)
                    {
                        m_result.more = true;
                        continue;
                    }
                    auto &network = m_result.networks[m_result.count++];
                    stringutils::safeCopy(network.ssid, name.c_str());
                    network.rssi = WiFi.RSSI(i);
                    network.encrypted = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
                }
            WiFi.scanDelete();
            WiFi.setAutoReconnect(reconnect);
            // Ohne Verbindung hat die Suche einen laufenden Versuch getrennt, ohne Neubeginn bliebe das WLAN bis zum Neustart getrennt.
            if (!connected)
                m_sequence.start();
        }
        else
        {
            m_sequence.interrupt();
            WiFi.disconnect(false, false);
            if (!m_work.profile.ssid[0] || !WiFi.STA.connect(m_work.profile.ssid, m_work.profile.password))
                m_result.error = Error::Service;
            else
            {
                LOG_PLAIN_F("WIFI_PROFILE CONNECTING");
                const auto started = millis();
                while (WiFi.status() != WL_CONNECTED && millis() - started < 20000 && m_cancel.load() != m_work.id)
                    vTaskDelay(pdMS_TO_TICKS(50));
                const auto ip = WiFi.localIP();
                if (m_cancel.load() == m_work.id)
                {
                    WiFi.disconnect(false, false);
                    m_result.outcome = Settings::Outcome::Cancelled;
                }
                else if (WiFi.status() == WL_CONNECTED && ip[0])
                {
                    for (int i = 0; i < 4; ++i)
                        m_result.ip[i] = ip[i];
                    LOG_PLAIN_F("WLAN VERBUNDEN IP=%s RSSI=%d", ip.toString().c_str(), WiFi.RSSI());
                }
                else
                {
                    m_result.error = WiFi.status() == WL_CONNECT_FAILED ? Error::Authentication : Error::NoNetwork;
                    LOG_PLAIN_F("WIFI_PROFILE ERROR_CONNECT");
                }
            }
        }
        m_wifiBusy.store(false);
        if (m_cancel.load() == m_work.id)
            m_result.outcome = Settings::Outcome::Cancelled;
        // Nach einem nicht erfolgreichen Test versucht Arduino weiter das Testnetz oder das WLAN bleibt getrennt, der Ablauf verbindet deshalb wieder die gespeicherten Profile.
        // Kennung UINT32_MAX ist der USB-Test, dessen erfolgreiche Verbindung bis zum Neustart gelten soll.
        if (m_work.type == Type::TestProfile)
            m_sequence.tested(m_result.error == Error::None && m_result.outcome == Settings::Outcome::Success, m_work.id == UINT32_MAX ? nullptr : m_work.profile.ssid);
    }
    else
        m_result.error = Error::Unavailable;
    if (m_result.error != Error::None)
        m_result.outcome = Settings::Outcome::Failed;
    mbedtls_platform_zeroize(&m_work, sizeof(m_work));
    LOG_INFO_F("DIENST Typ=%u Auftrag=%lu Ergebnis=%u Fehler=%u", static_cast<unsigned>(m_result.type), static_cast<unsigned long>(m_result.id), static_cast<unsigned>(m_result.outcome), static_cast<unsigned>(m_result.error));
    xQueueSend(m_results, &m_result, portMAX_DELAY);
    mbedtls_platform_zeroize(&m_result, sizeof(m_result));
}

SdServiceStatus AppServices::mediaStatus()
{
    SdServiceStatus s;
    s.revision = m_mediaRevision.load();
    s.mediaGeneration = m_mediaGeneration.load();
    s.media = m_media.load();
    const bool ready = s.media == SdServiceStatus::Media::Ready;
    s.list = s.collect = s.play = s.pause = s.seek = s.cover = ready;
    portENTER_CRITICAL(&m_configLock);
    s.totalBytes = m_sdTotal;
    s.usedBytes = m_sdUsed;
    portEXIT_CRITICAL(&m_configLock);
    s.retry = !ready && s.media != SdServiceStatus::Media::Mounting;
    return s;
}

void AppServices::mount()
{
    m_media.store(SdServiceStatus::Media::Mounting);
    m_mediaRevision.fetch_add(1);
    const bool ready = SD_MMC.setPins(43, 44, 39, 40, 41, 42) && SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_DEFAULT, 6);
    if (ready)
    {
        cleanupUploads();
        const uint64_t total = SD_MMC.totalBytes(), used = SD_MMC.usedBytes();
        portENTER_CRITICAL(&m_configLock);
        m_sdTotal = total;
        m_sdUsed = used;
        portEXIT_CRITICAL(&m_configLock);
    }
    m_card.mounted(ready, millis());
    m_media.store(ready ? SdServiceStatus::Media::Ready : SdServiceStatus::Media::Error);
    m_mediaRevision.fetch_add(1);
    LOG_INFO_F("SD Slot0: %s, keine Formatierung, Generation %lu", ready ? "bereit" : "nicht verfügbar", static_cast<unsigned long>(m_mediaGeneration.load()));
}

void AppServices::watchCard()
{
    if (m_retry.exchange(false))
        m_card.retry();
    switch (m_card.poll(millis(), audioUsesCard()))
    {
    case SdCardWatch::Step::Probe:
    {
        const uint32_t started = millis();
        if (!m_card.probed(cardAnswers()))
            break;
        m_mediaGeneration.store(m_card.generation());
        m_media.store(SdServiceStatus::Media::Error);
        m_mediaRevision.fetch_add(1);
        LOG_WARNING_F("SD-Karte antwortet nicht mehr (3 Statusabfragen in %lu ms), Generation %lu. Aushängen, sobald keine Audiodatei offen ist", static_cast<unsigned long>(millis() - started), static_cast<unsigned long>(m_card.generation()));
        break;
    }
    case SdCardWatch::Step::Unmount:
        if (m_webStore->uploading())
        {
            // Die Teildatei bleibt auf der Karte, mount() entfernt sie beim nächsten Einbinden.
            m_webStore->closeAll();
            m_fileLease.release(FileLease::Owner::Web);
        }
        // Das Medium ist hier nicht mehr bereit: Eine laufende Baumarbeit endet mit 503 und gibt die Kartensperre frei.
        stepWebTree();
        closeFtpFiles();
        SD_MMC.end();
        m_card.unmounted();
        LOG_INFO("SD Slot0: verlorene Karte ausgehängt");
        break;
    case SdCardWatch::Step::Mount:
        mount();
        break;
    case SdCardWatch::Step::None:
        break;
    }
}

bool AppServices::requestFiles(const SdFileRequest &request)
{
    if (request.kind == SdFileRequest::Kind::Cancel)
    {
        m_cancelFile.store(request.requestId);
        return true;
    }
    if (m_fileLease.held() || !m_files || m_media.load() != SdServiceStatus::Media::Ready || m_filesReady.load() != 0)
        return false;
    return xQueueSend(m_files, &request, 0) == pdPASS;
}

bool AppServices::takeFiles(SdFileResult &result)
{
    if (m_filesReady.load(std::memory_order_acquire) != 1)
        return false;
    result = m_fileResult;
    return true;
}

void AppServices::releaseFiles()
{
    m_filesReady.store(0, std::memory_order_release);
}

void AppServices::files()
{
    using State = SdFileResult::State;
    CardDirectory directory(m_cancelFile, m_fileWork.requestId, cardDrive());
    SdFileScan scan(m_entries, m_pending);
    State state = State::Error;
    // CardDirectory meldet einen Lesefehler wie das Ordnerende, ohne vorherige Statusabfrage erschiene eine verlorene Karte als leerer Ordner.
    if (m_fileWork.mediaGeneration == m_mediaGeneration.load() && m_media.load() == SdServiceStatus::Media::Ready && cardAnswers())
        state = m_fileWork.kind == SdFileRequest::Kind::List ? scan.list(directory, m_fileWork.path) : scan.collect(directory, m_fileWork.selection, m_fileWork.selectionCount);
    if (m_cancelFile.load() == m_fileWork.requestId)
        state = State::Cancelled;
    else if (state == State::Error)
        m_card.failed();
    if (scan.skipped())
        LOG_WARNING_F("SD %s %s: %u Einträge übersprungen (Pfad über 255 Byte, ungültiger Name oder tiefer als %u Ebenen)", m_fileWork.kind == SdFileRequest::Kind::List ? "Liste" : "Sammeln", m_fileWork.kind == SdFileRequest::Kind::List ? m_fileWork.path : m_fileWork.selection[0].path, scan.skipped(), SdFileScan::MaxDepth);
    m_fileResult = {};
    m_fileResult.requestId = m_fileWork.requestId;
    m_fileResult.mediaGeneration = m_fileWork.mediaGeneration;
    m_fileResult.kind = m_fileWork.kind;
    m_fileResult.entries = m_entries;
    m_fileResult.count = state == State::Data ? scan.count() : 0;
    m_fileResult.skipped = static_cast<uint16_t>(std::min(scan.skipped(), 0xffffu));
    m_fileResult.state = state;
    m_filesReady.store(1, std::memory_order_release);
}

void AppServices::worker(void *context)
{
    auto &s = *static_cast<AppServices *>(context);
    // Die Karte zuerst: Der SD-Player soll ohne WLAN sofort nach dem Start arbeiten.
    s.m_retry.store(false);
    s.mount();
    s.applyTime();
    // Jede IPv4-Zuteilung gilt als neue Verbindung, der Callback läuft im Arduino-Ereignistask.
    const NetworkEventCb connected = [](arduino_event_id_t)
    {
        s_connections.fetch_add(1);
    };
    WiFi.onEvent(connected, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    if (s.prepareWifi())
        s.m_sequence.start();
    for (;;)
    {
        if (xQueueReceive(s.m_requests, &s.m_work, pdMS_TO_TICKS(s.m_fileLease.held() ? 1 : 10)) == pdPASS)
            s.execute();
        if (xQueueReceive(s.m_modernRequests, &s.m_modernWork, 0) == pdPASS)
            s.executeModern();
        s.connectProfiles();
        s.updateNetwork();
        if (xQueueReceive(s.m_webFiles, &s.m_webWork, 0) == pdPASS)
            s.executeWebFile();
        s.stepWebTree();
        if (s.m_webStore->uploading() && millis() - s.m_webUploadAt >= 15000)
        {
            // Ein geschlossener Browser darf weder eine Teildatei noch die SD-Sperre behalten.
            s.m_webStore->abort();
            s.m_fileLease.release(FileLease::Owner::Web);
        }
        if (s.m_ftpJob.load(std::memory_order_acquire))
            s.executeFtpFile();
        if (const uint8_t password = s.m_ftpPasswordRequest.exchange(0))
            s.executeFtpPassword(password == 2);
        const uint32_t now = millis();
        if (s_rtcSyncPending.load() && (!s.m_rtcTimeAttempt || now - s.m_rtcTimeAttempt >= 1000))
        {
            s.m_rtcTimeAttempt = now;
            // Neue Benachrichtigungen während des Schreibens bleiben für den nächsten Durchlauf erhalten.
            s_rtcSyncPending.exchange(false);
            if (s.m_rtc.syncFromSystem())
            {
                s.m_rtcRefresh = now;
                LOG_INFO("Systemzeit als UTC in RTC bestätigt.");
            }
            else
            {
                s_rtcSyncPending.store(true);
                LOG_ERROR("RTC-Zeit nicht gespeichert, erneuter Versuch im Dienstworker.");
            }
        }
        if (!s_ntpSynced.load() && !s_rtcSyncPending.load() && now - s.m_rtcRefresh >= 60000)
        {
            s.m_rtcRefresh = now;
            if (s.m_rtc.restoreSystemTime())
                LOG_INFO("Offline-Systemzeit aus RTC nachgeführt.");
        }
        RadioPreferences radio;
        uint32_t generation, changed;
        portENTER_CRITICAL(&s.m_configLock);
        radio = s.m_radio;
        generation = s.m_radioGeneration;
        changed = s.m_radioChanged;
        portEXIT_CRITICAL(&s.m_configLock);
        // Erst nach dem Zustandsabbild messen, damit eine gleichzeitige UI-Änderung keine Zeitdifferenz unterlaufen lässt.
        const uint32_t radioNow = millis();
        if (s.m_rtc.status().available && generation != s.m_radioSaved && radioNow - s.m_rtcAttempt >= (s.m_rtcRadioFailed ? 1000u : 100u) && (radioNow - changed >= 100 || radioNow - s.m_rtcAttempt >= 500))
        {
            s.m_rtcAttempt = radioNow;
            s.m_rtcRadioFailed = !s.m_rtc.saveRadio(radio);
            if (!s.m_rtcRadioFailed)
                s.m_radioSaved = generation;
            else
                LOG_ERROR("RTC Radiozustand nicht gespeichert.");
        }
        const bool tuningChanged = radio.balance != s.m_radioNvs.balance || memcmp(radio.tone, s.m_radioNvs.tone, 3);
        const bool fallback = !s.m_rtc.status().available && generation != s.m_radioSaved;
        if ((tuningChanged || fallback) && radioNow - changed >= 1000 && radioNow - s.m_flashAttempt >= 1000)
        {
            s.m_flashAttempt = radioNow;
            if (Tab5Config::saveRadio(radio))
            {
                s.m_radioNvs = radio;
                ++s.m_radioFlashWrites;
                if (fallback)
                    s.m_radioSaved = generation;
                LOG_INFO("RADIO Balance/Klang in NVS gespeichert");
            }
            else
                LOG_ERROR("RADIO NVS-Speicherung nicht bestätigt.");
        }
        portENTER_CRITICAL(&s.m_configLock);
        s.m_rtcStatus = s.m_rtc.status();
        const Tab5Battery::Calibration battery = s.m_battery;
        const bool batteryPending = s.m_batteryPending;
        s.m_batteryPending = false;
        portEXIT_CRITICAL(&s.m_configLock);
        if (batteryPending)
        {
            if (Tab5Config::saveBattery(battery))
                LOG_INFO_F("AKKU Kennwerte gespeichert: %u mAh, %u mOhm", battery.capacityMah, battery.resistanceMohm);
            else
                LOG_ERROR("AKKU Kennwerte nicht gespeichert.");
        }
        s.watchCard();
        if (!s.m_coverReady.load(std::memory_order_acquire) && xQueueReceive(s.m_coverRequests, &s.m_coverWork, 0) == pdPASS)
            s.decodeCover();
        if (s.m_filesReady.load(std::memory_order_acquire) == 0 && xQueueReceive(s.m_files, &s.m_fileWork, 0) == pdPASS)
            s.files();
    }
}

RadioPreferences AppServices::radioPreferences()
{
    portENTER_CRITICAL(&m_configLock);
    const auto value = m_radio;
    portEXIT_CRITICAL(&m_configLock);
    return value;
}

void AppServices::saveBattery(const Tab5Battery::Calibration &value)
{
    portENTER_CRITICAL(&m_configLock);
    m_battery = value;
    m_batteryPending = true;
    portEXIT_CRITICAL(&m_configLock);
}

void AppServices::saveRadioPreferences(const RadioPreferences &value)
{
    portENTER_CRITICAL(&m_configLock);
    if (value.station != m_radio.station || value.mode != m_radio.mode || memcmp(value.styles, m_radio.styles, 8) || value.volume != m_radio.volume || memcmp(value.tone, m_radio.tone, 3) || value.balance != m_radio.balance || value.muted != m_radio.muted || value.beforeMute != m_radio.beforeMute)
    {
        m_radio = value;
        m_radioChanged = millis();
        ++m_radioGeneration;
    }
    portEXIT_CRITICAL(&m_configLock);
}

Tab5Rtc::Status AppServices::rtcStatus()
{
    portENTER_CRITICAL(&m_configLock);
    const auto status = m_rtcStatus;
    portEXIT_CRITICAL(&m_configLock);
    return status;
}

bool AppServices::modernColors(ModernFarben &colors)
{
    portENTER_CRITICAL(&m_configLock);
    colors = m_modern;
    const bool stored = m_modernStored;
    portEXIT_CRITICAL(&m_configLock);
    return stored;
}

bool AppServices::requestModern(const ModernRequest &request)
{
    return m_modernRequests && xQueueSend(m_modernRequests, &request, 0) == pdPASS;
}

bool AppServices::takeModernResult(ModernResult &result)
{
    return m_modernResults && xQueueReceive(m_modernResults, &result, 0) == pdPASS;
}

void AppServices::executeModern()
{
    ModernResult result;
    result.id = m_modernWork.id;
    if (!m_modernWork.reset && !Tab5Config::validModern(m_modernWork.colors))
        result.error = Settings::Error::Invalid;
    else
    {
        const bool written = m_modernWork.reset ? Tab5Config::resetModern() : Tab5Config::saveModern(m_modernWork.colors);
        // Rücklesen auch nach einem Fehler: Der Stand im RAM soll zeigen, was nach dem nächsten Neustart gilt.
        ModernFarben stored;
        const bool present = Tab5Config::loadModern(stored);
        bool confirmed = written && present != m_modernWork.reset;
        for (size_t i = ModernErsteBearbeitbare; confirmed && !m_modernWork.reset && i < ModernFarbenAnzahl; ++i)
            confirmed = stored.rgb[i] == m_modernWork.colors.rgb[i];
        portENTER_CRITICAL(&m_configLock);
        m_modern = stored;
        m_modernStored = present;
        portEXIT_CRITICAL(&m_configLock);
        result.error = confirmed ? Settings::Error::None : Settings::Error::Storage;
    }
    result.stored = modernColors(result.colors);
    xQueueSend(m_modernResults, &result, portMAX_DELAY);
}
