#include "FtpService.h"
#include "AudioManager.h"
#include "Logger.h"
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <lwip/sockets.h>
#include <fcntl.h>
#include <algorithm>
#include <cerrno>
#include <ctime>
#include <new>

namespace
{
    constexpr size_t SendBlock = 1024;            ///< Größter Sendeaufruf wie HTTP im Webportal, größere Blöcke erschöpften die internen Puffer des SDIO-Transports
    constexpr size_t TransportReserve = 32768;    ///< Wie im Webportal: darunter ruht der Datenkanal, damit dem SDIO-Transport interner RAM bleibt
    constexpr size_t TransportBlock = 4096;       ///< Ebenso der größte freie interne Block
    constexpr uint32_t SendPauseMs = 2;           ///< Pause nach jedem Sendeaufruf wie im Webportal
    constexpr size_t ReceiveBlock = 4096;         ///< Größter Empfangsaufruf, wie die 4-KiB-Uploadblöcke des Webportals
    constexpr uint32_t ReceivePauseMs = 24;       ///< Pause nach jedem Empfangsaufruf, höchstens rund 170 KB/s. Schnelleres Lesen öffnet das TCP-Fenster (64 KB) weiter, der C6 schickt größere Stöße und der SDIO-Empfangspuffer von esp_hosted wächst dauerhaft: mit dem Puffer im internen DMA-Speicher nach 8 MB Upload gemessen rund 80 KB freier DMA-Speicher bei 24 ms, 59 KB bei 14 ms, 55 KB bei 8 ms (vorher 104 KB). Ungebremst Assert in sdio_rx_get_buffer
    constexpr size_t UploadBlock = 32768;         ///< Kleinster größter freier interner DMA-Block, mit dem Uploaddaten angenommen werden. esp_hosted 2.11.6 (SDIO-Streaming) gibt beim Wachsen eines seiner zwei Empfangspuffer den alten frei und fordert danach bis zu 30.720 Byte am Stück an (20 Pakete des C6), über HostedMemory zuerst im PSRAM und nur ohne Platz dort aus diesem Speicher. Scheitert das, startet assert das Gerät neu. Karte und C6 teilen sich den SDMMC-Host samt seiner Sperre, Kartenschreiben kann das Abholen beim C6 verzögern
    constexpr uint32_t UploadMemoryWaitMs = 5000; ///< So lange wartet receive() ohne zu lesen, bis der Block wieder reicht. Danach bricht der Upload ab
    constexpr uint32_t StackBytes = 8192;         ///< Stack des FTP-Tasks im PSRAM
    constexpr uint32_t IdleWaitMs = 500;          ///< Wartezeit des ausgeschalteten Tasks bis zur nächsten Prüfung des Schalters
    constexpr uint32_t PollMs = 100;              ///< Wartezeit von select ohne Ereignis
    constexpr uint32_t LeaseAnswerMs = 2000;      ///< Wartezeit auf die Antwort des UI-Koordinators zur Kartensperre
    constexpr uint32_t ReplyTimeoutMs = 5000;     ///< Höchstdauer einer Antwort auf dem Steuerkanal
    constexpr uint32_t ReceiveFillMs = 200;       ///< receive() gibt einen teilweise gefüllten Puffer nach so langer Pause ohne Daten zurück
    constexpr uint32_t ServiceIntervalMs = 100;   ///< Abstand der Prüfung auf neue Verbindungen während einer Übertragung
    constexpr size_t DataBytes = 16384;           ///< Datenpuffer eines Kartenauftrags
    constexpr size_t TextBytes = 2048;            ///< Puffer für Listenzeilen

    /** @brief Prüft die Reserve an internem RAM für den SDIO-Transport wie sendBounded im Webportal. */
    bool transportReserved()
    {
        return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= TransportReserve && heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= TransportBlock;
    }

    /** @brief Schaltet einen Socket auf nicht blockierend. */
    void nonBlocking(int socket)
    {
        fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK);
    }

    /** @brief Wartet höchstens ms Millisekunden, bis ein Socket lesbar oder beschreibbar ist. */
    bool waitSocket(int socket, bool write, uint32_t ms)
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(socket, &set);
        timeval wait{static_cast<time_t>(ms / 1000), static_cast<suseconds_t>((ms % 1000) * 1000)};
        return select(socket + 1, write ? nullptr : &set, write ? &set : nullptr, nullptr, &wait) > 0;
    }

    /** @brief Liest Adresse und Netzmaske der WLAN-Station ohne C6-Aufruf. @return false ohne Adresse. */
    bool ownNetwork(uint8_t (&ip)[4], uint8_t (&mask)[4])
    {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t info{};
        if (!netif || esp_netif_get_ip_info(netif, &info) != ESP_OK || !info.ip.addr)
            return false;
        memcpy(ip, &info.ip.addr, 4);
        memcpy(mask, &info.netmask.addr, 4);
        return true;
    }

}

const char *FtpService::transferName(FtpSession::Transfer transfer)
{
    switch (transfer)
    {
    case FtpSession::Transfer::List:
        return "LIST";
    case FtpSession::Transfer::Retr:
        return "RETR";
    case FtpSession::Transfer::Stor:
        return "STOR";
    default:
        return "keiner";
    }
}

/** @brief Puffer und Sitzung des FTP-Dienstes, einmal im PSRAM angelegt. */
struct FtpService::Memory
{
    alignas(128) uint8_t data[DataBytes];                ///< Datenpuffer der Kartenaufträge. An der Cachezeile ausgerichtet, damit der SDMMC-DMA ohne Umweg hinein liest und daraus schreibt
    FtpProtocol::Job job;                                ///< Kartenauftrag der Sitzung
    FtpProtocol::Entry entries[FtpProtocol::ListBatch];  ///< Ziel von ListNext
    char text[TextBytes]{};                              ///< Listenzeilen
    char line[FtpProtocol::MaxLine + 1]{};               ///< Zuletzt gelesene Befehlszeile
    FtpProtocol::LineReader reader;                      ///< Zeilenaufbau des Steuerkanals
    uint8_t input[512]{};                                ///< Empfangene, noch nicht verarbeitete Bytes des Steuerkanals
    size_t inputStart = 0, inputEnd = 0;                 ///< Unverarbeiteter Bereich von input
    FtpSession session;                                  ///< Zustandsautomat der Verbindung

    /** @brief Verknüpft Auftrag, Puffer und Sitzung. */
    explicit Memory(FtpEnvironment &environment) : session(environment, job, text, sizeof(text))
    {
        job.data = data;
        job.capacity = sizeof(data);
        job.entries = entries;
        job.entryCapacity = FtpProtocol::ListBatch;
    }
};

bool FtpService::begin(AppServices &services)
{
    m_services = &services;
    void *memory = heap_caps_aligned_alloc(128, sizeof(Memory), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory)
        return false;
    m_memory = new (memory) Memory(*this);
    m_session = &m_memory->session;
    // Der Stack liegt im PSRAM: Der Task schreibt nie in den Flash (das Passwort speichert der Dienstworker), sonst wäre ein Stack im PSRAM bei abgeschaltetem Cache unzulässig.
    if (xTaskCreatePinnedToCoreWithCaps(task, "ftp", StackBytes, this, 1, &m_task, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS)
        return true;
    m_memory->~Memory();
    heap_caps_free(memory);
    m_memory = nullptr;
    m_session = nullptr;
    m_task = nullptr;
    return false;
}

void FtpService::setEnabled(bool enabled)
{
    m_wanted.store(enabled);
    portENTER_CRITICAL(&m_statusLock);
    m_status.enabled = enabled;
    portEXIT_CRITICAL(&m_statusLock);
    if (enabled && m_services)
        m_services->requestFtpPassword(false);
    if (m_task)
        xTaskNotifyGive(m_task);
}

FtpService::Status FtpService::status()
{
    portENTER_CRITICAL(&m_statusLock);
    Status copy = m_status;
    portEXIT_CRITICAL(&m_statusLock);
    char secret[FtpProtocol::PasswordCapacity]{};
    copy.password = password(secret);
    FtpProtocol::wipe(secret, sizeof(secret));
    uint8_t mask[4]{};
    if (!ownNetwork(copy.ip, mask))
        memset(copy.ip, 0, sizeof(copy.ip));
    copy.stackFree = m_task ? uxTaskGetStackHighWaterMark(m_task) : 0;
    return copy;
}

bool FtpService::password(char (&out)[FtpProtocol::PasswordCapacity])
{
    if (m_services && m_services->ftpPassword(out))
        return true;
    memset(out, 0, sizeof(out));
    return false;
}

bool FtpService::renewPassword()
{
    return m_services && m_services->requestFtpPassword(true);
}

void FtpService::task(void *context)
{
    static_cast<FtpService *>(context)->run();
}

void FtpService::run()
{
    for (;;)
    {
        if (!m_wanted.load())
        {
            if (m_listen >= 0)
            {
                stopListening();
                LOG_PLAIN_F("FTP AUS");
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(IdleWaitMs));
            continue;
        }
        char secret[FtpProtocol::PasswordCapacity]{};
        const bool ready = password(secret);
        FtpProtocol::wipe(secret, sizeof(secret));
        if (!ready)
        {
            // Der Dienstworker erzeugt das Passwort, bis dahin wird nicht gelauscht.
            m_services->requestFtpPassword(false);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
            continue;
        }
        if (m_listen < 0 && !listen())
        {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
            continue;
        }
        fd_set set;
        FD_ZERO(&set);
        FD_SET(m_listen, &set);
        int top = m_listen;
        if (m_control >= 0)
        {
            FD_SET(m_control, &set);
            top = std::max(top, m_control);
        }
        const bool pending = m_control >= 0 && m_memory->inputStart < m_memory->inputEnd;
        timeval wait{0, static_cast<suseconds_t>(pending ? 0 : PollMs * 1000)};
        const int events = select(top + 1, &set, nullptr, nullptr, &wait);
        if (events > 0 && FD_ISSET(m_listen, &set))
            acceptClient(m_control >= 0);
        if (m_control >= 0 && (pending || (events > 0 && FD_ISSET(m_control, &set))))
            handleControl();
        if (m_control >= 0 && !m_session->tick())
            endSession("Zeitüberschreitung");
        if (m_control >= 0 && m_session->loggedIn() && m_services->ftpPasswordRevision() != m_loginRevision)
        {
            m_session->close("421 Password changed, please log in again.\r\n");
            endSession("Passwort erneuert");
        }
        if (m_control >= 0 && m_controlBroken)
            endSession("Steuerkanal nicht beschreibbar");
        publish();
    }
}

bool FtpService::listen()
{
    const int socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket < 0)
    {
        LOG_ERROR_F("FTP Port %u nicht geöffnet, errno=%d", FtpProtocol::ControlPort, errno);
        return false;
    }
    const int yes = 1;
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(FtpProtocol::ControlPort);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || ::listen(socket, 1) != 0)
    {
        LOG_ERROR_F("FTP Port %u nicht geöffnet, errno=%d", FtpProtocol::ControlPort, errno);
        ::close(socket);
        return false;
    }
    nonBlocking(socket);
    m_listen = socket;
    portENTER_CRITICAL(&m_statusLock);
    m_status.listening = true;
    portEXIT_CRITICAL(&m_statusLock);
    LOG_PLAIN_F("FTP AN Port=%u Datenports=%u-%u Benutzer=%s", FtpProtocol::ControlPort, FtpProtocol::PassiveFirst, FtpProtocol::PassiveFirst + FtpProtocol::PassiveCount - 1, FtpProtocol::User);
    return true;
}

void FtpService::stopListening()
{
    if (m_control >= 0)
    {
        m_session->close("421 Service shutting down.\r\n");
        endSession("Dienst ausgeschaltet");
    }
    if (m_listen >= 0)
        ::close(m_listen);
    m_listen = -1;
    portENTER_CRITICAL(&m_statusLock);
    m_status.listening = false;
    portEXIT_CRITICAL(&m_statusLock);
}

void FtpService::acceptClient(bool busy)
{
    sockaddr_in peer{};
    socklen_t length = sizeof(peer);
    const int socket = ::accept(m_listen, reinterpret_cast<sockaddr *>(&peer), &length);
    if (socket < 0)
        return;
    uint8_t ip[4]{}, own[4]{}, mask[4]{};
    memcpy(ip, &peer.sin_addr.s_addr, 4);
    const bool local = ownNetwork(own, mask) && FtpProtocol::sameSubnet(ip, own, mask);
    nonBlocking(socket);
    if (busy || !local)
    {
        const char *text = busy ? "421 Only one client allowed, try again later.\r\n" : "421 Access only from the local network.\r\n";
        ::send(socket, text, strlen(text), MSG_DONTWAIT);
        ::close(socket);
        portENTER_CRITICAL(&m_statusLock);
        ++m_status.rejected;
        portEXIT_CRITICAL(&m_statusLock);
        LOG_PLAIN_F("FTP Verbindung von %u.%u.%u.%u abgewiesen (%s)", ip[0], ip[1], ip[2], ip[3], busy ? "nur ein Client" : "fremdes Netz");
        return;
    }
    m_control = socket;
    memcpy(m_peer, ip, sizeof(m_peer));
    m_controlBroken = false;
    m_memory->inputStart = m_memory->inputEnd = 0;
    m_memory->reader.clear();
    portENTER_CRITICAL(&m_statusLock);
    m_status.client = true;
    m_status.loggedIn = false;
    memcpy(m_status.clientIp, ip, sizeof(ip));
    ++m_status.sessions;
    portEXIT_CRITICAL(&m_statusLock);
    LOG_PLAIN_F("FTP Client %u.%u.%u.%u verbunden", ip[0], ip[1], ip[2], ip[3]);
    m_session->start();
}

bool FtpService::service()
{
    if (!m_wanted.load())
        return false;
    const uint64_t time = now();
    if (time - m_serviceAt >= ServiceIntervalMs)
    {
        m_serviceAt = time;
        acceptClient(true);
    }
    return true;
}

void FtpService::handleControl()
{
    for (;;)
    {
        const Control control = readLine(m_memory->line);
        if (control == Control::None)
            return;
        if (control == Control::Closed)
        {
            endSession("vom Client getrennt");
            return;
        }
        if (control == Control::Invalid)
        {
            m_session->rejectLine();
            continue;
        }
        const bool before = m_session->loggedIn();
        const uint32_t failures = m_session->failedLogins();
        const bool open = m_session->command(m_memory->line);
        FtpProtocol::wipe(m_memory->line, sizeof(m_memory->line));
        if (!before && m_session->loggedIn())
        {
            m_loginRevision = m_services->ftpPasswordRevision();
            LOG_PLAIN_F("FTP Anmeldung %u.%u.%u.%u", m_peer[0], m_peer[1], m_peer[2], m_peer[3]);
        }
        else if (m_session->failedLogins() != failures)
            LOG_WARNING_F("FTP Anmeldung von %u.%u.%u.%u abgewiesen", m_peer[0], m_peer[1], m_peer[2], m_peer[3]);
        publish();
        if (!open)
        {
            endSession(m_controlBroken ? "Steuerkanal nicht beschreibbar" : "beendet");
            return;
        }
        if (m_control < 0)
            return;
    }
}

void FtpService::endSession(const char *reason)
{
    closeData();
    if (m_control >= 0)
    {
        ::close(m_control);
        LOG_PLAIN_F("FTP Client %u.%u.%u.%u getrennt (%s)", m_peer[0], m_peer[1], m_peer[2], m_peer[3], reason);
    }
    m_control = -1;
    m_controlBroken = false;
    m_memory->reader.clear();
    FtpProtocol::wipe(m_memory->input, sizeof(m_memory->input));
    m_memory->inputStart = m_memory->inputEnd = 0;
    // Eine Sitzung gibt die Sperre am Ende jeder Kartenarbeit frei, das fängt nur einen Abbruch dazwischen ab.
    m_services->releaseFileLease(FileLease::Owner::Ftp);
    portENTER_CRITICAL(&m_statusLock);
    m_status.client = m_status.loggedIn = false;
    m_status.transfer = FtpSession::Transfer::None;
    portEXIT_CRITICAL(&m_statusLock);
}

FtpEnvironment::Control FtpService::readLine(char *out)
{
    auto &memory = *m_memory;
    for (;;)
    {
        while (memory.inputStart < memory.inputEnd)
        {
            const auto event = memory.reader.push(memory.input[memory.inputStart]);
            memory.input[memory.inputStart++] = 0;
            if (event == FtpProtocol::LineReader::Event::Line)
            {
                const char *line = memory.reader.line();
                const size_t length = strnlen(line, FtpProtocol::MaxLine);
                memcpy(out, line, length);
                out[length] = 0;
                memory.reader.clear();
                return Control::Line;
            }
            if (event == FtpProtocol::LineReader::Event::Invalid)
            {
                memory.reader.clear();
                return Control::Invalid;
            }
        }
        if (m_control < 0)
            return Control::Closed;
        const int count = ::recv(m_control, memory.input, sizeof(memory.input), MSG_DONTWAIT);
        if (count > 0)
        {
            memory.inputStart = 0;
            memory.inputEnd = static_cast<size_t>(count);
            continue;
        }
        memory.inputStart = memory.inputEnd = 0;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return Control::None;
        return Control::Closed;
    }
}

bool FtpService::sendControl(int socket, const char *text, size_t length)
{
    const uint64_t until = now() + ReplyTimeoutMs;
    for (size_t sent = 0; sent < length;)
    {
        const int count = ::send(socket, text + sent, length - sent, MSG_DONTWAIT);
        if (count > 0)
        {
            sent += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && now() < until)
        {
            waitSocket(socket, true, 50);
            continue;
        }
        return false;
    }
    return true;
}

void FtpService::publish()
{
    const bool loggedIn = m_control >= 0 && m_session->loggedIn();
    portENTER_CRITICAL(&m_statusLock);
    m_status.loggedIn = loggedIn;
    m_status.transfer = m_session->transfer();
    m_status.failedLogins = m_session->failedLogins();
    m_status.transfers = m_session->transfers();
    portEXIT_CRITICAL(&m_statusLock);
}

uint64_t FtpService::now()
{
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

int64_t FtpService::wallClock()
{
    return static_cast<int64_t>(time(nullptr));
}

void FtpService::reply(const char *text)
{
    if (m_control < 0 || m_controlBroken)
        return;
    if (!sendControl(m_control, text, strlen(text)))
        m_controlBroken = true;
}

FtpEnvironment::Lease FtpService::lease()
{
    const uint32_t id = m_leaseRequests.ask();
    const uint64_t until = now() + LeaseAnswerMs;
    FileLease::Answer answer = FileLease::Answer::Busy;
    while (!m_leaseRequests.answered(id, answer))
    {
        if (now() >= until)
        {
            if (m_leaseRequests.cancel(id))
                m_services->releaseFileLease(FileLease::Owner::Ftp);
            LOG_WARNING("FTP Kartensperre: keine Antwort des UI-Koordinators");
            return Lease::Timeout;
        }
        vTaskDelay(1);
    }
    if (answer == FileLease::Answer::Granted)
        return Lease::Granted;
    return answer == FileLease::Answer::Playing ? Lease::Playing : Lease::Busy;
}

void FtpService::release()
{
    m_services->releaseFileLease(FileLease::Owner::Ftp);
}

void FtpService::file(FtpProtocol::Job &job)
{
    m_services->ftpFile(job);
}

bool FtpService::passive(uint8_t (&ip)[4], uint16_t &port)
{
    closeData();
    sockaddr_in local{};
    socklen_t length = sizeof(local);
    // Die Adresse des Steuerkanals gilt auch nach einem Wechsel der IPv4-Adresse.
    if (m_control < 0 || getsockname(m_control, reinterpret_cast<sockaddr *>(&local), &length) != 0)
        return false;
    memcpy(ip, &local.sin_addr.s_addr, 4);
    for (uint16_t i = 0; i < FtpProtocol::PassiveCount; ++i)
    {
        const uint16_t index = (m_nextPort + i) % FtpProtocol::PassiveCount;
        const int socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket < 0)
            return false;
        const int yes = 1;
        setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(FtpProtocol::PassiveFirst + index);
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0 && ::listen(socket, 1) == 0)
        {
            nonBlocking(socket);
            m_passive = socket;
            m_nextPort = (index + 1) % FtpProtocol::PassiveCount;
            port = FtpProtocol::PassiveFirst + index;
            return true;
        }
        ::close(socket);
    }
    LOG_WARNING("FTP kein passiver Port frei");
    return false;
}

bool FtpService::accept()
{
    const uint64_t until = now() + FtpProtocol::DataConnectTimeoutMs;
    while (m_passive >= 0 && now() < until)
    {
        if (!service())
            return false;
        if (!waitSocket(m_passive, false, 100))
            continue;
        sockaddr_in peer{};
        socklen_t length = sizeof(peer);
        const int socket = ::accept(m_passive, reinterpret_cast<sockaddr *>(&peer), &length);
        if (socket < 0)
            continue;
        // Nur die Gegenstelle des Steuerkanals darf Daten holen oder ablegen.
        if (memcmp(&peer.sin_addr.s_addr, m_peer, sizeof(m_peer)))
        {
            const uint8_t *ip = reinterpret_cast<const uint8_t *>(&peer.sin_addr.s_addr);
            LOG_WARNING_F("FTP Datenverbindung von %u.%u.%u.%u abgewiesen", ip[0], ip[1], ip[2], ip[3]);
            ::close(socket);
            continue;
        }
        nonBlocking(socket);
        ::close(m_passive);
        m_passive = -1;
        m_data = socket;
        m_dataEnd = false;
        portENTER_CRITICAL(&m_statusLock);
        m_status.bytes = 0;
        portEXIT_CRITICAL(&m_statusLock);
        return true;
    }
    return false;
}

bool FtpService::waitData(bool write, bool interruptible)
{
    const bool control = interruptible && m_control >= 0;
    if (control && m_memory->inputStart < m_memory->inputEnd)
        return true;
    fd_set reads, writes;
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    FD_SET(m_data, write ? &writes : &reads);
    if (control)
        FD_SET(m_control, &reads);
    timeval wait{0, 50000};
    return select(std::max(m_data, control ? m_control : -1) + 1, &reads, &writes, nullptr, &wait) > 0 && control && FD_ISSET(m_control, &reads);
}

FtpEnvironment::Data FtpService::send(const uint8_t *data, size_t length, size_t &sent, bool interruptible)
{
    sent = 0;
    uint64_t progress = now();
    while (sent < length)
    {
        if (!service())
            return Data::Shutdown;
        if (now() - progress >= FtpProtocol::DataStallTimeoutMs)
            return Data::Timeout;
        if (!transportReserved())
        {
            vTaskDelay(pdMS_TO_TICKS(SendPauseMs));
            continue;
        }
        const int count = ::send(m_data, data + sent, std::min(SendBlock, length - sent), MSG_DONTWAIT);
        if (count > 0)
        {
            sent += static_cast<size_t>(count);
            progress = now();
            portENTER_CRITICAL(&m_statusLock);
            m_status.bytes += static_cast<uint64_t>(count);
            m_status.transfer = m_session->transfer();
            portEXIT_CRITICAL(&m_statusLock);
            vTaskDelay(pdMS_TO_TICKS(SendPauseMs));
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            if (waitData(true, interruptible))
                return Data::Control;
            continue;
        }
        return Data::Closed;
    }
    return Data::Ok;
}

FtpEnvironment::Data FtpService::receive(uint8_t *data, size_t capacity, size_t &received, bool interruptible)
{
    received = 0;
    if (m_dataEnd)
        return Data::Ok;
    uint64_t progress = now(), lowSince = 0;
    while (received < capacity)
    {
        if (!service())
            return Data::Shutdown;
        const uint64_t time = now();
        if (time - progress >= FtpProtocol::DataStallTimeoutMs)
            return Data::Timeout;
        // Ohne Lesen schließt sich das TCP-Fenster und der Client hält an. Ein vorübergehend belegter Block (etwa eine Portalanfrage) wird abgewartet, ein Radiostream nicht.
        const Upload allowed = uploadAllowed();
        if (allowed == Upload::Radio)
            return Data::Radio;
        if (allowed == Upload::Memory)
        {
            if (!lowSince)
                lowSince = time;
            else if (time - lowSince >= UploadMemoryWaitMs)
                return Data::Memory;
            vTaskDelay(pdMS_TO_TICKS(SendPauseMs));
            continue;
        }
        lowSince = 0;
        // Wie beim Senden ruht der Datenkanal bei knappem internem RAM. Das TCP-Fenster bremst den Client, bis der SDIO-Transport wieder Platz hat.
        if (!transportReserved())
        {
            vTaskDelay(pdMS_TO_TICKS(SendPauseMs));
            continue;
        }
        const int count = ::recv(m_data, data + received, std::min(ReceiveBlock, capacity - received), MSG_DONTWAIT);
        if (count > 0)
        {
            received += static_cast<size_t>(count);
            progress = time;
            portENTER_CRITICAL(&m_statusLock);
            m_status.bytes += static_cast<uint64_t>(count);
            m_status.transfer = m_session->transfer();
            portEXIT_CRITICAL(&m_statusLock);
            vTaskDelay(pdMS_TO_TICKS(ReceivePauseMs));
            continue;
        }
        if (count == 0)
        {
            m_dataEnd = true;
            return Data::Ok;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            return Data::Closed;
        // Ein teilweise gefüllter Puffer geht nach kurzer Pause an die Karte, damit ein langsamer Client nicht in die Zeitüberschreitung läuft.
        if (received && time - progress >= ReceiveFillMs)
            return Data::Ok;
        if (waitData(false, interruptible))
            return Data::Control;
    }
    return Data::Ok;
}

FtpEnvironment::Upload FtpService::uploadAllowed()
{
    const auto audio = g_audioManager.getStatus();
    if (!audio.file && (audio.state == AudioManager::State::Starting || audio.state == AudioManager::State::Streaming))
        return Upload::Radio;
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) >= UploadBlock ? Upload::Ok : Upload::Memory;
}

void FtpService::closeData()
{
    if (m_data >= 0)
        ::close(m_data);
    if (m_passive >= 0)
        ::close(m_passive);
    m_data = m_passive = -1;
    m_dataEnd = false;
}

FtpEnvironment::Control FtpService::poll(char *line)
{
    return readLine(line);
}

void FtpService::wait(uint32_t ms)
{
    vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(ms)));
}

void FtpService::transferred(const char *command, const char *path, const char *reply, uint64_t bytes)
{
    LOG_PLAIN_F("FTP %s %s Bytes=%llu Antwort=%s", command, path, static_cast<unsigned long long>(bytes), reply);
}
