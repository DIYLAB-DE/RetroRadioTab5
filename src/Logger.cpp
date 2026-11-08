#include "Logger.h"

#if LOGGER_ENABLED
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <esp_rom_sys.h>
#include <esp_vfs.h>
#include <freertos/ringbuf.h>
#include <sys/stat.h>
// Wie in Arduinos HWCDC.cpp: Die Registerzugriffe des Headers sind in C++20 als volatile-Verbundzuweisung veraltet.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvolatile"
#include <hal/usb_serial_jtag_ll.h>
#pragma GCC diagnostic pop

namespace
{
    constexpr size_t QueueBytes = 8192;              ///< Zeilenwarteschlange im internen RAM, der Start erzeugt in 5 s rund 4 KB
    constexpr size_t ItemBytes = 512;                ///< Größter Eintrag, längere stdout-Blöcke gehen in Teilen
    constexpr uint32_t WriterStack = 3072;           ///< Stack des Schreibtasks in Byte
    constexpr UBaseType_t WriterPriority = 2;        ///< Über der Arduino-Hauptschleife (1), damit Zeilen ohne Tick-Verzögerung abfließen
    constexpr uint32_t UsbCheckMs = 20;              ///< Längster Abstand zwischen zwei Prüfungen auf eine stehengebliebene USB-Ausgabe in ms
    constexpr size_t RomLineBytes = 160;             ///< Zeilenpuffer der ROM-Ausgabe je Kern und Kontext, längere Zeilen gehen in Teilen
    constexpr const char *OutputPath = "/dev/rrlog"; ///< VFS-Pfad, auf den stdout und stderr umgelenkt werden
    constexpr size_t StdioBufferBytes = 256;         ///< Zeilenpuffer von stdout und stderr, eine Zeile bis zu dieser Länge geht in einem Stück

    RingbufHandle_t s_queue = nullptr;  ///< Warteschlange zum Schreibtask, nullptr vor Logger::init oder wenn die Anlage scheiterte
    std::atomic<uint32_t> s_dropped{0}; ///< Wegen voller Warteschlange verworfene Ausgaben seit der letzten Meldung
    std::atomic<bool> s_usbWritten{false}; ///< Seit dem letzten Nullpaket ging eine Ausgabe auf Serial

    /** @brief Zeile der ROM-Ausgabe im Aufbau. */
    struct RomLine
    {
        char text[RomLineBytes]; ///< Gesammelte Zeichen ohne Terminator
        size_t length;           ///< Belegte Bytes
        void *owner;             ///< Task, der die Zeile begonnen hat. nullptr im Interrupt
    };

    RomLine s_romLines[portNUM_PROCESSORS][2];             ///< Je Kern eine Zeile für Tasks [0] und eine für Interrupts [1]
    portMUX_TYPE s_romLock = portMUX_INITIALIZER_UNLOCKED; ///< Schützt s_romLines gegen Tasks und Interrupts beider Kerne

    /** @brief Übergibt eine fertige Ausgabe ohne Warten an die Warteschlange, vor der Anlage direkt an Serial. Eine volle Warteschlange verwirft die Ausgabe und zählt sie. Nicht aus Interrupts aufrufen. */
    void emit(const char *data, size_t length)
    {
        if (!length)
            return;
        if (!s_queue)
        {
            Serial.write(reinterpret_cast<const uint8_t *>(data), length);
            return;
        }
        if (xRingbufferSend(s_queue, data, length, 0) != pdTRUE)
            s_dropped.fetch_add(1, std::memory_order_relaxed);
    }

    /** @brief Schließt eine mit vsnprintf formatierte Zeile mit \r\n ab und gibt sie aus. Bei Formatfehler, leerem Ergebnis oder zu wenig Platz kommt stattdessen eine Fehlermeldung. @param buffer Puffer des Loggers. @param size Puffergröße. @param prefix Länge des Präfixes vor dem Formatergebnis. @param written Rückgabe von vsnprintf. */
    void emitFormatted(char *buffer, size_t size, size_t prefix, int written)
    {
        if (written > 0 && prefix + static_cast<size_t>(written) + 2 <= size)
        {
            const size_t length = prefix + static_cast<size_t>(written);
            buffer[length] = '\r';
            buffer[length + 1] = '\n';
            emit(buffer, length + 2);
            return;
        }
        static constexpr char Overflow[] = "[LOG ERROR: Buffer overflow]\r\n";
        emit(Overflow, sizeof(Overflow) - 1);
    }

    /** @brief Gibt den terminierten Text im Puffer aus, mit newline wird \r\n angehängt und der Text dafür bei Bedarf gekürzt. @param buffer Puffer mit mindestens 3 Byte. @param size Puffergröße. @param newline Zeilenende anhängen. */
    void emitText(char *buffer, size_t size, bool newline)
    {
        size_t length = strnlen(buffer, size - 1);
        if (newline)
        {
            length = std::min(length, size - 3);
            buffer[length++] = '\r';
            buffer[length++] = '\n';
        }
        emit(buffer, length);
    }

    /** @brief Hält Arduinos USB-Ausgabe (HWCDC) in Gang, wo sie von selbst stehen bleibt. Schreibt dazu keine Daten, sondern sendet höchstens ein Nullpaket und gibt IN_EMPTY frei. Auslöser ist meist die SOF-Überwachung der ESP-IDF, die gelegentlich für einen Tick „getrennt“ meldet: Trifft das den HWCDC-Interrupt, löscht er IN_EMPTY, ohne die FIFO nachzufüllen, und es kommt kein Ereignis mehr (Interrupt frei, Rohbit fehlt, FIFO leer). Trifft es ein Serial.write, legt HWCDC die Daten nur in den Sendepuffer, ohne den gesperrten Interrupt freizugeben. Beides löst erst der nächste Schreibaufruf, Bildübertragungen warten aber auf Platz im Sendepuffer. Außerdem schließt HWCDC eine Ausgabe, die mit einem vollen 64-Byte-Paket endet, nicht mit einem Nullpaket ab, und der Host hält das Paket zurück, bis weitere Daten kommen. Nur aus dem Schreibtask auf dem Kern des USB-Interrupts aufrufen, damit der Interrupt nicht mitten im Füllen der FIFO steht. */
    void serviceUsb()
    {
        constexpr uint32_t InEmpty = USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY;
        // Ein Paket ist unterwegs, seine Abholung löst IN_EMPTY aus.
        if (!usb_serial_jtag_ll_txfifo_writable())
            return;
        if (usb_serial_jtag_ll_get_intr_ena_status() & InEmpty)
        {
            // Freigegeben, aber ohne Rohbit und ohne Paket unterwegs: Das Ereignis ist verloren.
            if (!(usb_serial_jtag_ll_get_intraw_mask() & InEmpty))
                usb_serial_jtag_ll_txfifo_flush();
        }
        else if (s_usbWritten.exchange(false, std::memory_order_relaxed))
        {
            // Gesperrt hat sich der Interrupt bei leerem Sendepuffer selbst, oder HWCDC hielt sich beim Schreiben für getrennt. Wie in HWCDC::write schließt das Abschließen der FIFO die Ausgabe ab, die Freigabe holt Liegengebliebenes ab. Ist nichts mehr da, sperrt sich der Interrupt wieder.
            usb_serial_jtag_ll_txfifo_flush();
            usb_serial_jtag_ll_ena_intr_mask(InEmpty);
        }
    }

    /** @brief Einziger Textschreiber auf Serial. Läuft auf dem Kern des USB-Interrupts, damit kein Schreibaufruf ein Paket abschließt, das der Interrupt gerade füllt. Ruft spätestens alle UsbCheckMs serviceUsb auf, auch für die Bildpakete der Hauptschleife. */
    void writerTask(void *)
    {
        for (;;)
        {
            size_t size = 0;
            void *item = xRingbufferReceive(s_queue, &size, pdMS_TO_TICKS(UsbCheckMs));
            serviceUsb();
            if (!item)
                continue;
            Serial.write(static_cast<const uint8_t *>(item), size);
            s_usbWritten.store(true, std::memory_order_relaxed);
            vRingbufferReturnItem(s_queue, item);
            const uint32_t dropped = s_dropped.exchange(0, std::memory_order_relaxed);
            if (dropped)
            {
                char note[48];
                const int length = snprintf(note, sizeof(note), "[LOG] %lu Ausgaben verworfen\r\n", static_cast<unsigned long>(dropped));
                Serial.write(reinterpret_cast<const uint8_t *>(note), std::min(static_cast<size_t>(std::max(length, 0)), sizeof(note) - 1));
            }
        }
    }

    /** @brief Gibt eine ROM-Zeile an die Warteschlange und leert sie, nur unter s_romLock. */
    void IRAM_ATTR romFlush(RomLine &line)
    {
        // Unter s_romLock sind die Interrupts gesperrt. Dort ist nur die ISR-Variante zulässig, sie wartet nie. Ein geweckter Schreibtask läuft beim nächsten Taskwechsel.
        if (line.length && xRingbufferSendFromISR(s_queue, line.text, line.length, nullptr) != pdTRUE)
            s_dropped.fetch_add(1, std::memory_order_relaxed);
        line.length = 0;
    }

    /** @brief Zeichenausgabe der ROM-Funktion ets_printf (Arduinos log_x, esp_rom_printf). Sammelt je Kern ganze Zeilen und hängt \r\n an. Läuft in beliebigen Tasks und Interrupts, auch bei abgeschaltetem Flash-Cache, deshalb im IRAM. */
    void IRAM_ATTR romPutc(char c)
    {
        if (c == '\r')
            return;
        const bool isr = xPortInIsrContext();
        void *const owner = isr ? nullptr : xTaskGetCurrentTaskHandle();
        portENTER_CRITICAL_SAFE(&s_romLock);
        RomLine &line = s_romLines[xPortGetCoreID()][isr ? 1 : 0];
        // Ein anderer Task desselben Kerns hat die Zeile unterbrochen: das Begonnene zuerst abgeben, damit sich zwei Zeilen nicht vermischen.
        if (line.length && line.owner != owner)
            romFlush(line);
        line.owner = owner;
        // Nach jedem Zeichen bleiben mindestens drei Bytes frei, genug für ein weiteres Zeichen oder \r\n.
        if (c == '\n')
        {
            line.text[line.length++] = '\r';
            line.text[line.length++] = '\n';
        }
        else
            line.text[line.length++] = c;
        if (c == '\n' || line.length >= RomLineBytes - 2)
            romFlush(line);
        portEXIT_CRITICAL_SAFE(&s_romLock);
    }

    /** @brief VFS-Schreibfunktion für stdout und stderr, ein Block bis ItemBytes wird eine Ausgabe. */
    ssize_t stdioWrite(int, const void *data, size_t size)
    {
        const char *text = static_cast<const char *>(data);
        for (size_t done = 0; done < size;)
        {
            const size_t part = std::min(size - done, ItemBytes);
            emit(text + done, part);
            done += part;
        }
        return static_cast<ssize_t>(size);
    }

    /** @brief VFS-Öffnen, es gibt nur den einen Ausgabekanal. */
    int stdioOpen(const char *, int, int)
    {
        return 0;
    }

    /** @brief VFS-Schließen ohne Wirkung. */
    int stdioClose(int)
    {
        return 0;
    }

    /** @brief Meldet ein Zeichengerät, damit newlib stdout und stderr zeilenweise puffert. */
    int stdioFstat(int, struct stat *info)
    {
        memset(info, 0, sizeof(*info));
        info->st_mode = S_IFCHR;
        return 0;
    }

    /** @brief Legt Warteschlange und Schreibtask an und leitet ROM-Ausgabe, stdout und stderr dorthin um. Ohne Warteschlange bleibt alles beim direkten Weg. */
    void startOutput()
    {
        s_queue = xRingbufferCreateWithCaps(QueueBytes, RINGBUF_TYPE_NOSPLIT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s_queue)
            return;
        if (xTaskCreatePinnedToCore(writerTask, "log-usb", WriterStack, nullptr, WriterPriority, nullptr, xPortGetCoreID()) != pdPASS)
        {
            vRingbufferDeleteWithCaps(s_queue);
            s_queue = nullptr;
            return;
        }
        esp_rom_install_channel_putc(1, romPutc);
        esp_rom_install_channel_putc(2, nullptr);
        esp_vfs_t vfs{};
        vfs.flags = ESP_VFS_FLAG_DEFAULT;
        vfs.write = stdioWrite;
        vfs.open = stdioOpen;
        vfs.close = stdioClose;
        vfs.fstat = stdioFstat;
        // freopen schließt den alten Kanal vor dem Öffnen, ohne registrierten Pfad bliebe stdout sonst tot.
        if (esp_vfs_register(OutputPath, &vfs, nullptr) != ESP_OK)
            return;
        // freopen behält das FILE-Objekt, das sich alle Tasks über ihre Reentrancy-Struktur teilen.
        for (FILE *stream : {stdout, stderr})
            if (freopen(OutputPath, "w", stream))
                setvbuf(stream, nullptr, _IOLBF, StdioBufferBytes);
    }
} // namespace

Logger *Logger::_instance = nullptr;

const Logger::Tag Logger::TAGS[11] = {
    {"info", Logger::BLUE},
    {"warning", Logger::YELLOW},
    {"error", Logger::RED},
    {"success", Logger::GREEN},
    {"custom", Logger::CYAN},
    {"bitrate", Logger::MAGENTA},
    {"titel", Logger::CYAN},
    {"sender", Logger::BRIGHT_MAGENTA},
    {"audio", Logger::BRIGHT_BLUE},
    {"radio", Logger::BRIGHT_GREEN},
    {"settings", Logger::WHITE}};

Logger &Logger::init(bool enabled, bool withTime, size_t bufferSize)
{
    if (_instance == nullptr)
    {
        _instance = new Logger(enabled, withTime, bufferSize);
        startOutput();
    }

    return *_instance;
}

Logger &Logger::getInstance()
{
    assert(_instance != nullptr && "Logger not initialized! Call Logger::init() first.");

    return *_instance;
}

bool Logger::isInitialized()
{
    return _instance != nullptr;
}

void Logger::usbOutputWritten()
{
    s_usbWritten.store(true, std::memory_order_relaxed);
}

Logger::Logger(bool enabled, bool withTime, size_t bufferSize)
    : _enabled(enabled), _withTime(withTime), _start(millis()), _bufferSize(bufferSize)
{
    _mtx = xSemaphoreCreateMutex();
    _buffer = new char[_bufferSize];
}

Logger::~Logger()
{
    if (_mtx)
        vSemaphoreDelete(_mtx);

    delete[] _buffer;
}

void Logger::setEnabled(bool enabled)
{
    _enabled.store(enabled, std::memory_order_relaxed);
}

Logger::AnswerScope::AnswerScope()
{
    if (_instance)
        _instance->_answerTask.store(xTaskGetCurrentTaskHandle(), std::memory_order_relaxed);
}

Logger::AnswerScope::~AnswerScope()
{
    if (_instance)
        _instance->_answerTask.store(nullptr, std::memory_order_relaxed);
}

bool Logger::allowed() const
{
    return _enabled.load(std::memory_order_relaxed) || _answerTask.load(std::memory_order_relaxed) == xTaskGetCurrentTaskHandle();
}

void Logger::setTimeOutput(bool enabled)
{
    _withTime = enabled;
}

void Logger::resetTime()
{
    _start = millis();
}

void Logger::print(Level level, const char *msg)
{
    lock();
    logDirect(level, msg, false);
    unlock();
}

void Logger::println(Level level, const char *msg)
{
    lock();
    logDirect(level, msg, true);
    unlock();
}

void Logger::printf(Level level, const char *format, ...)
{
    lock();

    if (!allowed())
    {
        unlock();
        return;
    }

    size_t pos = buildPrefix(level);

    va_list args;
    va_start(args, format);
    int written = vsnprintf(_buffer + pos, _bufferSize - pos, format, args);
    va_end(args);

    emitFormatted(_buffer, _bufferSize, pos, written);

    unlock();
}

void Logger::printfPlain(const char *format, ...)
{
    lock();

    if (!allowed())
    {
        unlock();
        return;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(_buffer, _bufferSize, format, args);
    va_end(args);

    emitFormatted(_buffer, _bufferSize, 0, written);

    unlock();
}

void Logger::printlnCustom(const char *tag, const char *msg, const char *color)
{
    lock();
    logCustomDirect(tag, msg, color, true);
    unlock();
}

void Logger::printCustom(const char *tag, const char *msg, const char *color)
{
    lock();
    logCustomDirect(tag, msg, color, false);
    unlock();
}

size_t Logger::buildPrefix(Level level)
{
    const Tag &t = TAGS[level];
    size_t pos = 0;

    pos += snprintf(_buffer + pos, _bufferSize - pos, "[%s%s%s] ", t.color, t.name, RESET);

    if (_withTime && pos < _bufferSize)
    {
        char timeStr[16];
        formatMillisDirect(millis() - _start, timeStr, sizeof(timeStr));
        pos += snprintf(_buffer + pos, _bufferSize - pos, "%s[%s]%s ", BRIGHT_BLACK, timeStr, RESET);
    }

    return pos;
}

size_t Logger::buildCustomPrefix(const char *tag, const char *color)
{
    size_t pos = 0;

    pos += snprintf(_buffer + pos, _bufferSize - pos, "[%s%s%s] ", color, tag, RESET);

    if (_withTime && pos < _bufferSize)
    {
        char timeStr[16];
        formatMillisDirect(millis() - _start, timeStr, sizeof(timeStr));
        pos += snprintf(_buffer + pos, _bufferSize - pos, "%s[%s]%s ", BRIGHT_BLACK, timeStr, RESET);
    }

    return pos;
}

void Logger::logDirect(Level level, const char *msg, bool newline)
{
    if (!allowed())
        return;

    size_t pos = buildPrefix(level);

    snprintf(_buffer + pos, _bufferSize - pos, "%s", msg);

    emitText(_buffer, _bufferSize, newline);
}

void Logger::logCustomDirect(const char *tag, const char *msg, const char *color, bool newline)
{
    if (!allowed())
        return;

    size_t pos = buildCustomPrefix(tag, color);

    snprintf(_buffer + pos, _bufferSize - pos, "%s", msg);

    emitText(_buffer, _bufferSize, newline);
}

void Logger::formatMillisDirect(uint32_t ms, char *buf, size_t bufSize) const
{
    uint16_t minutes = ms / 60000;
    uint8_t seconds = (ms / 1000) % 60;
    uint16_t millisRest = ms % 1000;
    snprintf(buf, bufSize, "%02u:%02u.%03u", minutes, seconds, millisRest);
}

void Logger::lock()
{
    if (_mtx == nullptr)
        _mtx = xSemaphoreCreateMutex();

    if (_mtx)
        xSemaphoreTake(_mtx, portMAX_DELAY);
}

void Logger::unlock()
{
    if (_mtx)
        xSemaphoreGive(_mtx);
}

#else

const Logger::Tag Logger::TAGS[11] = {
    {"", ""},
    {"", ""},
    {"", ""},
    {"", ""},
    {"", ""},
    {"", ""},
    {"", ""},
    {"", ""},
    {"", ""},
    {"", ""},
    {"", ""}};

#endif
