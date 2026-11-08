#ifndef AUDIO_LITE_PSRAM_UNIQUE_PTR_H
#define AUDIO_LITE_PSRAM_UNIQUE_PTR_H

/**
 * @file psram_unique_ptr.hpp
 * @brief Besitzender Zeiger auf Speicher im PSRAM
 *
 * Portiert aus der Referenz-Library ESP32-audioI2S 3.4.7. Ersetzt sind die Arduino-Abhängigkeiten: `ps_malloc()` und `ps_calloc()` durch die IDF-Entsprechungen und die Arduino-Log-Makros durch `ESP_LOGx`. Die Kommentare sind eingedeutscht. Die Code-Token entsprechen weiterhin dem Upstream, ein inhaltlicher Abgleich bleibt damit möglich.
 */

#include <esp_heap_caps.h>
#include <esp_log.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

// ===================================================================
// PSRAM-Allokation
// ===================================================================

inline constexpr const char *PS_PTR_TAG = "ps_ptr"; ///< Log-Kennung dieses Headers

/**
 * @brief Meldet, ob nutzbarer PSRAM vorhanden ist
 * @return true wenn der Heap PSRAM ausweist
 *
 * Tritt an die Stelle von Arduinos `psram_found()`.
 */
inline bool psram_found()
{
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
}

/**
 * @brief Allokiert Speicher im PSRAM
 * @param size Größe in Bytes
 * @return Zeiger auf den Speicher, oder nullptr wenn kein PSRAM frei ist
 *
 * Tritt an die Stelle von Arduinos `ps_malloc()` und verhält sich genauso, insbesondere ohne Rückfall auf den internen Heap. Der abweichende Name ist notwendig: audio-lite baut im Arduino-Framework, und ein gleichnamiges Symbol würde mit der Deklaration in `esp32-hal-psram.h` kollidieren.
 */
inline void *psram_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

/**
 * @brief Allokiert nullgesetzten Speicher im PSRAM
 * @param count Anzahl der Elemente
 * @param size Größe eines Elements in Bytes
 * @return Zeiger auf den Speicher, oder nullptr wenn kein PSRAM frei ist
 */
inline void *psram_calloc(size_t count, size_t size)
{
    return heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}


#ifndef PS_PTR_CLASS
    #define PS_PTR_CLASS 1

// Hilfsfunktionen für Unique-Pointer im PSRAM des ESP32.
//
// Verwendungsbeispiele:
//   Einzelobjekt:   auto pint = ps_make_unique<int>(123);
//   Einzelobjekt:   auto pstr = ps_make_unique<std::string>("Hallo PSRAM");
//   char-Puffer:    auto buf = ps_make_unique<char>(256); std::strcpy(buf.get(), "Text im PSRAM");
//   int-Puffer:     auto iarr = ps_make_unique<int>(64); iarr[0] = 42;

// ===================================================================
// PsramDeleter
// ===================================================================

// Deleter für PSRAM-Speicher, gibt mit free() frei (funktioniert für PSRAM wie für internen Heap)
struct PsramDeleter {
    void operator()(void* ptr) const noexcept {
        if (ptr) {
            free(ptr);
        }
    }
};

// Erzeugt ein Einzelobjekt im PSRAM
template <typename T, typename... Args> std::unique_ptr<T, PsramDeleter> ps_make_unique(Args&&... args) {
    void* raw = psram_malloc(sizeof(T));
    if (!raw) {
        throw std::bad_alloc();
    }

    // Objekt mit Placement-new im geholten Speicher konstruieren
    T* obj = new (raw) T(std::forward<Args>(args)...);

    return std::unique_ptr<T, PsramDeleter>(obj);
}

// Erzeugt ein Array von Objekten im PSRAM, liefert bei Speichermangel nullptr statt einer Ausnahme
template <typename T> std::unique_ptr<T[], PsramDeleter> ps_make_unique(size_t count) {
    T* raw = static_cast<T*>(psram_malloc(sizeof(T) * count));
    if (!raw) {
        printf("OOM: ps_malloc failed (%zu bytes)\n", sizeof(T) * count);
        return std::unique_ptr<T[], PsramDeleter>(nullptr);
    }
    return std::unique_ptr<T[], PsramDeleter>(raw);
}

// Vergleicht zwei Zeichenketten ohne Beachtung der Groß-/Kleinschreibung, höchstens n Zeichen
inline int strncasecmp_local(const char* s1, const char* s2, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        unsigned char c1 = static_cast<unsigned char>(s1[i]);
        unsigned char c2 = static_cast<unsigned char>(s2[i]);
        if (tolower(c1) != tolower(c2))
            return tolower(c1) - tolower(c2);
        if (c1 == '\0')
            break;
    }
    return 0;
}

template <typename T>

class ps_ptr {
  private:
    std::unique_ptr<T[], PsramDeleter> mem;
    size_t allocated_size = 0;
    char* name = nullptr;    ///< Objektname für Diagnosemeldungen
    static inline T dummy{}; ///< Rückgabeziel für ungültige Zugriffe
    size_t length_ = 0;      ///< tatsächliche Zeichenanzahl
  public:
    // Setzt den Objektnamen für Diagnosemeldungen
    void set_name(const char* new_name) {
        if (name) {
            free(name);
            name = nullptr;
        }
        if (new_name) {
            std::size_t len = std::strlen(new_name) + 1;
            if (psram_found()) {
                name = static_cast<char*>(psram_malloc(len));
            } else {
                name = static_cast<char*>(malloc(len));
            }
            if (name) {
                std::memcpy(name, new_name, len);
            } else {
                printf("OOM: failed to allocate %zu bytes for name %s\n", len, new_name);
            }
        }
    }

    ps_ptr() = default;

    ~ps_ptr() {
        if (name) {
            free(name);
            name = nullptr;
        }
    }

    explicit operator bool() const {
        return mem.get() != nullptr;
    }

    // Konstruktor für C-Strings, nur für ps_ptr<char> zulässig
    ps_ptr(const char* src) {
        if constexpr (std::is_same_v<T, char>) {
            assign(src);
        } else {
            static_assert(!std::is_same_v<T, char>, "This constructor is only available for ps_ptr<char>");
        }
    }

    // Konstruktor aus Zeichenkette mit bekannter Länge, hängt die Nullterminierung selbst an
    ps_ptr(const char* src, size_t len) {
        if (src && len > 0) {
            allocated_size = len + 1;
            mem = ps_make_unique<char>(allocated_size);
            if (mem.get() && allocated_size > len) {
                std::memcpy(mem.get(), src, len);
    // allocated_size ist len + 1, der Index [len] ist deshalb gültig. Die Warnung sieht das nicht
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wstringop-overflow"
                mem.get()[len] = '\0';
    #pragma GCC diagnostic pop
            } else {
                allocated_size = 0;
            }
        }
    }

    ps_ptr(ps_ptr&& other) noexcept {
        mem = std::move(other.mem);
        allocated_size = other.allocated_size;
        name = other.name;
        other.allocated_size = 0;
        other.name = nullptr;
    }

    // Kopierkonstruktor, tiefe Kopie, nur für ps_ptr<char> zulässig
    ps_ptr(const ps_ptr& other) {
        if constexpr (std::is_same_v<T, char>) {
            assign(other.get());
        } else {
            static_assert(!std::is_same_v<T, T>, "Copy constructor disabled for this type");
        }
    }

    // Allokierender Konstruktor, zum Beispiel ps_ptr<char> buff(1024)
    explicit ps_ptr(size_t n) {
        alloc(n);
    }
    // ===================================================================
    // Allokation
    // ===================================================================

    // Allokiert size Bytes, bevorzugt im PSRAM. Verwendung:
    //   ps_ptr<char> test;
    //   test.alloc(100);
    //   if (test.valid()) { test.clear(), }
    bool alloc(std::size_t size, const char* alloc_name = nullptr, bool usePSRAM = true) {
        size = (size + 15) & ~15; // auf 16 Bytes ausrichten
        if (psram_found() && usePSRAM) {
            mem.reset(static_cast<T*>(psram_malloc(size)));
        } else {
            mem.reset(static_cast<T*>(malloc(size)));
        }
        if (alloc_name) {
            set_name(alloc_name);
        }
        if (!mem) {
            allocated_size = 0;
            printf("OOM: failed to allocate %zu bytes for %s\n", size, name ? name : "unnamed");
            return false;
        }
        // allocated_size erst nach erfolgreicher Allokation setzen: ein Fehlschlag hinterließe sonst einen Nullzeiger mit großer Größe, und der geprüfte operator[] dereferenzierte nullptr plus Index.
        allocated_size = size;
        return true;
    }

    // Allokiert ein Einzelobjekt, ruft dessen Konstruktor über Placement-new auf
    bool alloc(const char* alloc_name = nullptr) {
        reset(); // zuvor gehaltenen Speicher freigeben
        void* raw_mem = nullptr;
        if (psram_found()) {
            raw_mem = psram_malloc(sizeof(T));
        } else {
            raw_mem = malloc(sizeof(T));
        }
        if (alloc_name) {
            set_name(alloc_name);
        }
        if (raw_mem) {
            mem.reset(new (raw_mem) T());
            allocated_size = sizeof(T);
        } else {
            printf("OOM: failed to allocate %zu bytes for %s\n", sizeof(T), name ? name : "unnamed");
            allocated_size = 0;
            return false;
        }
        return true;
    }

    // Allokiert Platz für num_elements Elemente und setzt ihn auf null, bevorzugt im PSRAM
    bool calloc(std::size_t num_elements, const char* alloc_name = nullptr, bool usePSRAM = true) {
        size_t total_size = num_elements * sizeof(T);
        total_size = (total_size + 15) & ~15; // auf 16 Bytes ausrichten

        reset(); // zuvor gehaltenen Speicher freigeben
        if (alloc_name) {
            set_name(alloc_name);
        }
        void* raw_mem = nullptr;

        if (psram_found() && usePSRAM) {
            raw_mem = psram_malloc(total_size);
        } else {
            raw_mem = malloc(total_size);
        }

        if (raw_mem) {
            memset(raw_mem, 0, total_size);
            mem.reset(static_cast<T*>(raw_mem));
            allocated_size = total_size;
        } else {
            printf("OOM: failed to calloc %zu bytes for %s\n", total_size, name ? name : "unnamed");
            allocated_size = 0;
            return false;
        }
        return true;
    }
    // ===================================================================
    // Allokiert ein Array mit count Elementen, ohne Initialisierung
    bool alloc_array(std::size_t count, const char* alloc_name = nullptr) {
        if (alloc_name) {
            set_name(alloc_name);
        }
        bool res = alloc(sizeof(T) * count);
        return res;
    }

    // Allokiert ein Array mit count Elementen und value-initialisiert jedes Element
    bool calloc_array(std::size_t count, const char* alloc_name = nullptr) {
        if (alloc_name) {
            set_name(alloc_name);
        }

        bool res = alloc(sizeof(T) * count);
        if (!res) {
            return false;
        }

        for (std::size_t i = 0; i < count; i++) {
            new (&(get()[i])) T{};
        }
        return true;
    }

    // Setzt den gesamten allokierten Block auf null, wirkungslos ohne Allokation
    void zero_mem() {
        if (mem) {
            memset(mem.get(), 0, allocated_size);
        }
    }

    // Vergrößert oder verkleinert den Block auf new_size Bytes und kopiert den Inhalt um
    void realloc(size_t new_size) {
        void* new_mem = nullptr;
        if (psram_found()) {
            new_mem = psram_malloc(new_size);
        } else {
            new_mem = malloc(new_size);
        }
        if (!new_mem) {
            printf("OOM: failed to realloc %zu bytes for %s\n", new_size, name ? name : "unnamed");
            return;
        }

        if (mem && allocated_size > 0) {
            std::memcpy(new_mem, mem.get(), std::min(allocated_size, new_size));
        }

        mem.reset(static_cast<T*>(new_mem));
        allocated_size = new_size;
    }
    // ===================================================================
    // Zuweisen und Kopieren
    // ===================================================================

    // Kopiert die nullterminierte Zeichenkette in eigenen Speicher, nur für ps_ptr<char>
    void assign(const char* src) {
        if constexpr (std::is_same_v<T, char>) {
            if (!src) {
                reset();
                return;
            }
            std::size_t len = std::strlen(src) + 1;
            alloc(len);
            if (mem) {
                std::memcpy(mem.get(), src, len);
            }
        } else {
            static_assert(std::is_same_v<T, char>, "assign(const char*) is only valid for ps_ptr<char>");
        }
    }

    // Kopiert höchstens max_len Zeichen und terminiert selbst, wie strndup(), nur für ps_ptr<char>
    void assign(const char* src, std::size_t max_len) {
        static_assert(std::is_same_v<T, char>, "assign(const char*, size_t) is only valid for ps_ptr<char>");
        if (!src) {
            reset();
            return;
        }
        std::size_t actual_len = strnlen(src, max_len);
        std::size_t total = actual_len + 1;
        alloc(total);
        if (mem) {
            std::memcpy(mem.get(), src, actual_len);
            static_cast<char*>(mem.get())[actual_len] = '\0';
        }
    }

    // Kopiert count Elemente vom externen Zeiger in eigenen Speicher und hängt ein Nullelement an
    void copy_from(const T* src, std::size_t count) {
        std::size_t bytes = (count + 1) * sizeof(T); // +1 für die Nullterminierung
        alloc(bytes);
        if (mem && src) {
            std::memcpy(mem.get(), src, count * sizeof(T));
            mem.get()[count] = '\0';
        }
    }

    // Kopiert eine nullterminierte Zeichenkette und meldet die kopierte Bytezahl
    size_t copy_from(const T* src) {
        if (src == nullptr) {
            ESP_LOGE(PS_PTR_TAG, "arg. is null");
            return 0;
        }
        std::size_t count = std::strlen(src) + 1;
        std::size_t bytes = count * sizeof(T);
        alloc(bytes);
        if (mem && src) {
            std::memcpy(mem.get(), src, bytes);
        }
        return bytes;
    }

    // Wandelt UTF-16 (LE oder BE, mit oder ohne BOM) nach UTF-8 und stoppt am Doppel-Null-Terminator. UTF-16 kommt vor allem in ID3-Headern vor, zum Beispiel wird die Folge 4C 00 69 00 74 00 ... 6C 00 00 00 (UTF-16LE) zu "Little London Girl".
    size_t copy_from_utf16(const uint8_t* src, bool is_big_endian = false) {
        if (!src) {
            ESP_LOGE(PS_PTR_TAG, "arg. is null");
            return 0;
        }
        std::vector<char> out;
        size_t i = 0;

        // BOM-Handling
        if (src[i] == 0xFF && src[i + 1] == 0xFE) {
            is_big_endian = false; // UTF-16LE
            i += 2;
        } else if (src[i] == 0xFE && src[i + 1] == 0xFF) {
            is_big_endian = true; // UTF-16BE
            i += 2;
        }

        while (true) {
            // Prüfe, ob genug Bytes für ein UTF-16-Zeichen vorhanden sind
            if (i + 1 >= std::numeric_limits<size_t>::max() || (src[i] == 0x00 && src[i + 1] == 0x00)) {
                break; // Nullterminator oder Ende des Puffers
            }

            uint16_t ch;
            if (is_big_endian) {
                ch = (src[i] << 8) | src[i + 1];
            } else {
                ch = (src[i + 1] << 8) | src[i];
            }
            i += 2;

            uint32_t codepoint = ch;

            // Prüfe auf Surrogatenpaare
            if (ch >= 0xD800 && ch <= 0xDBFF) { // High surrogate
                if ((i + 1 >= std::numeric_limits<size_t>::max()) || (src[i] == 0x00 && src[i + 1] == 0x00)) {
                    ESP_LOGE(PS_PTR_TAG, "Invalid surrogate pair: missing low surrogate");
                    break;
                }
                uint16_t ch2;
                if (is_big_endian) {
                    ch2 = (src[i] << 8) | src[i + 1];
                } else {
                    ch2 = (src[i + 1] << 8) | src[i];
                }
                if (ch2 < 0xDC00 || ch2 > 0xDFFF) {
                    ESP_LOGE(PS_PTR_TAG, "Invalid surrogate pair: invalid low surrogate");
                    break;
                }
                i += 2;
                codepoint = 0x10000 + ((ch - 0xD800) << 10) + (ch2 - 0xDC00);
            } else if (ch >= 0xDC00 && ch <= 0xDFFF) {
                ESP_LOGE(PS_PTR_TAG, "Invalid surrogate pair: unexpected low surrogate");
                break;
            }

            // UTF-16 -> UTF-8
            if (codepoint < 0x80) {
                out.push_back(static_cast<char>(codepoint));
            } else if (codepoint < 0x800) {
                out.push_back(0xC0 | (codepoint >> 6));
                out.push_back(0x80 | (codepoint & 0x3F));
            } else if (codepoint < 0x10000) {
                out.push_back(0xE0 | (codepoint >> 12));
                out.push_back(0x80 | ((codepoint >> 6) & 0x3F));
                out.push_back(0x80 | (codepoint & 0x3F));
            } else if (codepoint < 0x110000) {
                out.push_back(0xF0 | (codepoint >> 18));
                out.push_back(0x80 | ((codepoint >> 12) & 0x3F));
                out.push_back(0x80 | ((codepoint >> 6) & 0x3F));
                out.push_back(0x80 | (codepoint & 0x3F));
            } else {
                ESP_LOGE(PS_PTR_TAG, "Invalid codepoint");
                break;
            }
        }

        // Nullterminator hinzufügen
        out.push_back('\0');

        // Speicher allozieren und kopieren
        std::size_t bytes = out.size();
        if (!alloc(bytes))
            return 0;
        std::memcpy(mem.get(), out.data(), bytes);
        return i;
    }

    // Wandelt ISO 8859-1 nach UTF-8 und stoppt an der Nullterminierung
    size_t copy_from_iso8859_1(const uint8_t* src) {
        if (!src) {
            ESP_LOGE(PS_PTR_TAG, "arg. is null");
            return 0;
        }
        std::vector<char> out;
        size_t i = 0;

        while (true) {
            uint8_t ch = src[i];
            if (ch == 0x00) {
                break;
            }

            // ISO-8859-1 -> UTF-8
            if (ch < 0x80) {
                out.push_back(static_cast<char>(ch)); // ASCII-Bereich bleibt unverändert
            } else {
                // Zeichen von 0x80 bis 0xFF werden in UTF-8 zu Zwei-Byte-Folgen
                out.push_back(0xC0 | (ch >> 6));
                out.push_back(0x80 | (ch & 0x3F));
            }
            i++;
        }

        out.push_back('\0');

        std::size_t bytes = out.size();
        if (!alloc(bytes))
            return 0;
        std::memcpy(mem.get(), out.data(), bytes);
        return i;
    }

    // Dekodiert die gehaltene Zeichenkette am Platz aus der URL-Kodierung, zum Beispiel wird "Born%20On%20The%20B.mp3" zu "Born On The B.mp3" und "A+Test.mp3" zu "A Test.mp3"
    void urldecode() {
        static_assert(std::is_same_v<T, char>, "urldecode() is only valid for ps_ptr<char>");
        if (!mem || !get()) {
            ESP_LOGE(PS_PTR_TAG, "urldecode: No valid string data");
            return;
        }

        char* str = get();
        uint16_t p1 = 0, p2 = 0;
        char a, b;

        while (str[p1]) {
            if ((str[p1] == '%') && ((a = str[p1 + 1]) && (b = str[p1 + 2])) && (isxdigit(a) && isxdigit(b))) {

                // Kleinbuchstaben auf Großbuchstaben normalisieren
                if (a >= 'a')
                    a -= ('a' - 'A');
                if (b >= 'a')
                    b -= ('a' - 'A');

                // Hex-Ziffern in Zahlenwerte wandeln
                a = (a >= 'A') ? (a - 'A' + 10) : (a - '0');
                b = (b >= 'A') ? (b - 'A' + 10) : (b - '0');

                str[p2++] = (a << 4) | b;
                p1 += 3;
            } else if (str[p1] == '+') {
                str[p2++] = ' ';
                p1++;
            } else {
                str[p2++] = str[p1++];
            }
        }
        str[p2] = '\0';
    }
    // Übernimmt eine tiefe Kopie des anderen Objekts, ein leeres Original setzt auch dieses Objekt zurück
    void clone_from(const ps_ptr<T>& other) {
        if (!other.valid() || other.size() == 0) {
            reset();
            return;
        }

        if constexpr (std::is_same_v<T, char>) {
            assign(other.get());
            return;
        }

        std::size_t sz = other.size();
        alloc(sz);
        if (mem && sz > 0) {
            std::memcpy(mem.get(), other.get(), sz);
        }
    }
    // Tauscht Inhalt und Größe mit dem anderen Objekt
    void swap(ps_ptr<T>& other) noexcept {
        std::swap(this->mem, other.mem);
        std::swap(this->allocated_size, other.allocated_size);
    }

    // Tauscht den gehaltenen Speicher gegen einen rohen Zeiger, der bisherige geht in den Besitz des Aufrufers über
    void swap_with_pointer(T*& raw_ptr) noexcept {
        T* temp = get();
        mem.release(); // Besitz aufgeben, ohne zu löschen
        mem = std::unique_ptr<T, PsramDeleter>(raw_ptr);
        raw_ptr = temp;
    }
    // ===================================================================
    // Anhängen und Formatieren
    // ===================================================================

    // Hängt die Zeichenkette an, wie strcat() mit automatischer Neuallokation
    template <typename U = T>
        requires std::is_same_v<U, char>
    void append(const char* suffix) {
        if (!suffix || !*suffix)
            return;

        std::size_t old_len = mem ? std::strlen(static_cast<char*>(mem.get())) : 0;
        std::size_t add_len = std::strlen(suffix);
        std::size_t new_len = old_len + add_len + 1; // +1 für die Nullterminierung

        // alten Speicher übernehmen
        char* old_data = static_cast<char*>(mem.release());

        if (psram_found()) {
            mem.reset(static_cast<T*>(psram_malloc(new_len)));
        } else {
            mem.reset(static_cast<T*>(malloc(new_len)));
        }
        if (!mem) {
            printf("OOM: append() failed for %zu bytes\n", new_len);
            return;
        }

        // bisherigen Inhalt kopieren
        if (old_data) {
            std::memcpy(mem.get(), old_data, old_len);
            free(old_data);
        }

        // Suffix anhängen
        std::memcpy(static_cast<char*>(mem.get()) + old_len, suffix, add_len + 1);
        allocated_size = new_len;
        static_cast<char*>(mem.get())[old_len + add_len] = '\0';
    }

    // Hängt höchstens len Zeichen der Zeichenkette an und terminiert selbst
    template <typename U = T>
        requires std::is_same_v<U, char>
    void append(const char* suffix, std::size_t len) {
        if (!suffix || len == 0)
            return;

        std::size_t old_len = mem ? std::strlen(static_cast<char*>(mem.get())) : 0;
        std::size_t new_len = old_len + len + 1; // +1 für die Nullterminierung

        char* old_data = static_cast<char*>(mem.release());

        if (psram_found()) { // Check at the runtime whether PSRAM is available
            mem.reset(static_cast<T*>(psram_malloc(new_len)));
        } else {
            mem.reset(static_cast<T*>(malloc(new_len)));
        }
        if (!mem) {
            printf("OOM: append(len) failed for %zu bytes\n", new_len);
            return;
        }

        if (old_data) {
            std::memcpy(mem.get(), old_data, old_len);
            free(old_data);
        }

        std::memcpy(static_cast<char*>(mem.get()) + old_len, suffix, len);
        static_cast<char*>(mem.get())[old_len + len] = '\0';
    }

    // Hängt den Inhalt eines anderen ps_ptr<char> an
    template <typename U = T>
        requires std::is_same_v<U, char>
    void append(const ps_ptr<char>& other) {
        const char* suffix = other.get();
        if (!suffix || !*suffix)
            return; // nichts anzuhängen

        size_t add_len = std::strlen(suffix);
        size_t old_len = mem ? std::strlen(static_cast<char*>(mem.get())) : 0;
        size_t new_len = old_len + add_len + 1;

        char* old_data = static_cast<char*>(mem.release());

        if (psram_found()) {
            mem.reset(static_cast<T*>(psram_malloc(new_len)));
        } else {
            mem.reset(static_cast<T*>(malloc(new_len)));
        }

        if (!mem) {
            printf("OOM: append(ps_ptr) failed for %zu bytes\n", new_len);
            if (old_data)
                free(old_data);
            return;
        }

        if (old_data) {
            std::memcpy(mem.get(), old_data, old_len);
            free(old_data);
        }

        std::memcpy(mem.get() + old_len, suffix, add_len + 1);
        allocated_size = new_len;
    }
    // Hängt ein einzelnes Zeichen an und verdoppelt bei Bedarf die Kapazität
    void push_back(char c) {
        if (length_ + 1 >= capacity()) {
            size_t new_cap = (capacity() == 0) ? 16 : capacity() * 2;
            reserve(new_cap);
        }

        mem.get()[length_++] = c;
        mem.get()[length_] = '\0';
    }

    // Tatsächliche Zeichenanzahl
    size_t length() const {
        return length_;
    }

    // Nutzbare Kapazität ohne die Nullterminierung
    size_t capacity() const {
        return allocated_size ? allocated_size - 1 : 0;
    }

    // Stellt Platz für mindestens new_cap Zeichen sicher und erhält den Inhalt
    void reserve(size_t new_cap) {
        if (new_cap + 1 <= allocated_size)
            return; // genug Platz vorhanden

        char* old_data = mem.release();
        size_t old_len = length();

        size_t new_size = new_cap + 1; // +1 für '\0'
        if (psram_found())
            mem.reset(static_cast<char*>(psram_malloc(new_size)));
        else
            mem.reset(static_cast<char*>(malloc(new_size)));

        if (!mem) {
            printf("OOM: reserve(%zu) failed\n", new_size);
            if (old_data)
                free(old_data);
            return;
        }

        if (old_data) {
            if (old_len > 0)
                std::memcpy(mem.get(), old_data, old_len + 1);
            else
                mem.get()[0] = '\0';
            free(old_data);
        } else {
            mem.get()[0] = '\0';
        }

        allocated_size = new_size;
    }
    // ===================================================================
    // Vergleichen und Suchen
    // ===================================================================

    // Beginnt die Zeichenkette mit dem Präfix?
    template <typename U = T>
        requires std::is_same_v<U, char>
    bool starts_with(const char* prefix) const {
        if (!mem || !prefix)
            return false;

        const char* str = static_cast<const char*>(mem.get());
        std::string_view sv(str);
        return sv.starts_with(prefix);
    }

    // Endet die Zeichenkette mit dem Suffix?
    template <typename U = T>
        requires std::is_same_v<U, char>
    bool ends_with(const char* prefix) const {
        if (!mem || !prefix)
            return false;

        const char* str = static_cast<const char*>(mem.get());
        std::string_view sv(str);
        return sv.ends_with(prefix);
    }

    // Beginnt die Zeichenkette mit dem Präfix, ohne Beachtung der Groß-/Kleinschreibung?
    template <typename U = T>
        requires std::is_same_v<U, char>
    bool starts_with_icase(const char* prefix) const {
        if (!mem || !prefix)
            return false;

        const char* str = static_cast<const char*>(mem.get());
        std::size_t prefix_len = std::strlen(prefix);
        std::size_t str_len = std::strlen(str);
        if (prefix_len > str_len)
            return false;

        return strncasecmp_local(str, prefix, prefix_len) == 0;
    }
    // Endet die Zeichenkette mit dem Suffix, ohne Beachtung der Groß-/Kleinschreibung?
    template <typename U = T>
        requires std::is_same_v<U, char>
    bool ends_with_icase(const char* suffix) const {
        if (!mem || !suffix)
            return false;

        const char* str = static_cast<const char*>(mem.get());
        std::size_t suffix_len = std::strlen(suffix);
        std::size_t str_len = std::strlen(str);
        if (suffix_len > str_len)
            return false;

        return strncasecmp_local(str + str_len - suffix_len, suffix, suffix_len) == 0;
    }
    // Sind beide Zeichenketten gleich?
    bool equals(const ps_ptr<T>& other) const {
        if (!this->valid() || !other.valid())
            return false;
        if (!this->get() || !other.get())
            return false;
        return strcmp(this->get(), other.get()) == 0;
    }

    bool equals(const char* other) const {
        if (!this->valid())
            return false;
        const char* myStr = this->get();
        if (!myStr || !other)
            return false;
        return strcmp(myStr, other) == 0;
    }
    // Formatiert im {}-Stil in eigenen Speicher, zum Beispiel assignf("v={:02} hex={:04X} pi={:.2f}", 7, 0xAF, 3.14159) -> "v=07 hex=00AF pi=3.14". bool wird zu "true" oder "false"
    template <typename U = T, typename... Args>
        requires std::is_same_v<U, char>
    void assignf(const char* fmt, Args&&... args) {

        if (!fmt)
            return;

        constexpr size_t arg_count = sizeof...(Args);

        size_t placeholder_count = count_placeholders(fmt);

        if (placeholder_count != arg_count) {
            printf("assignf(): \033[31m placeholder mismatch (expected %zu, got %zu), content: '%s' \033[0m\n", placeholder_count, arg_count, fmt);
            return;
        }

        std::string tmp;

        format_append(tmp, fmt, std::forward<Args>(args)...);

        reset();

        alloc(tmp.size() + 1);

        if (!mem) {
            printf("OOM: assignf() failed for %zu bytes\n", tmp.size() + 1);
            return;
        }

        memcpy(mem.get(), tmp.c_str(), tmp.size() + 1);

        allocated_size = tmp.size();
    }

    // Formatiert im {}-Stil und hängt das Ergebnis an den vorhandenen Text an
    template <typename U = T, typename... Args>
        requires std::is_same_v<U, char>
    void appendf(const char* fmt, Args&&... args) {
        if (!fmt)
            return;

        constexpr size_t arg_count = sizeof...(Args);

        size_t placeholder_count = count_placeholders(fmt);

        if (placeholder_count != arg_count) {
            printf("appendf(): \033[31m placeholder mismatch (expected %zu, got %zu), content: '%s' \033[0m\n", placeholder_count, arg_count, fmt);
            return;
        }

        std::string tmp;
        format_append(tmp, fmt, std::forward<Args>(args)...);
        std::size_t old_len = mem ? std::strlen(mem.get()) : 0;
        std::size_t add_len = tmp.size();
        std::size_t new_len = old_len + add_len + 1;
        char* old_data = static_cast<char*>(mem.release());
        reset();
        alloc(new_len);
        if (!mem) {
            printf("OOM: appendf1() failed for %zu bytes\n", new_len);
            if (old_data) {
                free(old_data);
            }
            return;
        }

        // alten Text kopieren
        if (old_data && old_len > 0) {
            std::memcpy(mem.get(), old_data, old_len);
            free(old_data);
        }

        // neuen Text anhängen
        std::memcpy(mem.get() + old_len, tmp.c_str(), add_len + 1);
    }

    // Sucht ein Element ab start und liefert seinen Index, -1 wenn nicht gefunden. Allgemeine Fassung für beliebige T
    int index_of(const T& value, std::size_t start = 0) const {
        if (!mem || allocated_size < sizeof(T))
            return -1;

        std::size_t count = allocated_size / sizeof(T);
        if (start >= count)
            return -1;

        T* data = get();
        for (std::size_t i = start; i < count; ++i) {
            if (data[i] == value)
                return static_cast<int>(i);
        }
        return -1;
    }

    // Sucht ein einzelnes Zeichen ab start, nur für ps_ptr<char>
    template <typename U = T>
        requires std::is_same_v<U, char>
    int index_of(char ch, std::size_t start = 0) const {
        if (!mem)
            return -1;

        const char* str = static_cast<const char*>(mem.get());
        std::size_t len = std::strlen(str);
        if (start >= len)
            return -1;

        for (std::size_t i = start; i < len; ++i) {
            if (str[i] == ch)
                return static_cast<int>(i);
        }
        return -1;
    }
    // Sucht eine Teilzeichenkette ab start, nur für ps_ptr<char>
    template <typename U = T>
        requires std::is_same_v<U, char>
    int index_of(const char* substr, std::size_t start = 0) const {
        if (!mem || !substr || !*substr)
            return -1;

        const char* str = static_cast<const char*>(mem.get());
        std::size_t len = std::strlen(str);
        if (start >= len)
            return -1;

        const char* found = std::strstr(str + start, substr);
        if (!found)
            return -1;

        return static_cast<int>(found - str);
    }
    // Sucht die Bytefolge needle in den ersten max_length Bytes des Puffers und liefert den Abstand des ersten Treffers vom Pufferanfang, -1 wenn nicht gefunden. Nullbytes in Puffer und Suchmuster sind erlaubt, damit taugt die Suche auch für Binärdaten.
    int32_t special_index_of(const char* needle, uint32_t needle_length, uint32_t max_length) const {
        static_assert(std::is_same_v<T, char>, "index_of is only valid for ps_ptr<char>");
        if (!mem || !get()) {
            ESP_LOGE(PS_PTR_TAG, "index_of: No valid buffer data");
            return -1;
        }
        if (!needle || needle_length == 0) {
            ESP_LOGE(PS_PTR_TAG, "index_of: Invalid needle (null or empty)");
            return -1;
        }
        if (max_length < needle_length) {
            ESP_LOGE(PS_PTR_TAG, "index_of: max_length (%u) too short to find needle of length %u", max_length, needle_length);
            return -1;
        }
        const char* data = get();
        for (uint32_t i = 0; i <= max_length - needle_length; ++i) {
            if (std::memcmp(data + i, needle, needle_length) == 0) {
                return static_cast<int32_t>(i);
            }
        }
        ESP_LOGD(PS_PTR_TAG, "index_of: Needle not found within %lu bytes", max_length);
        return -1;
    }

    // Überladung für nullterminierte Suchmuster, die Länge wird selbst bestimmt
    template <typename U = T>
        requires std::is_same_v<U, char>
    int32_t special_index_of(const char* needle, uint32_t max_length) const {
        return special_index_of(needle, needle ? std::strlen(needle) : 0, max_length);
    }

    // Sucht die Bytefolge needle rückwärts und liefert den Abstand des letzten Treffers, -1 wenn nicht gefunden
    int32_t last_special_index_of(const char* needle, uint32_t needle_length, uint32_t max_length) const {
        static_assert(std::is_same_v<T, char>, "last_special_index_of is only valid for ps_ptr<char>");

        if (!mem || !get()) {
            ESP_LOGE(PS_PTR_TAG, "last_special_index_of: No valid buffer data");
            return -1;
        }

        if (!needle || needle_length == 0) {
            ESP_LOGE(PS_PTR_TAG, "last_special_index_of: Invalid needle (null or empty)");
            return -1;
        }

        if (max_length < needle_length) {
            ESP_LOGE(PS_PTR_TAG, "last_special_index_of: max_length (%u) too short to find needle of length %u", max_length, needle_length);
            return -1;
        }

        const char* data = get();

        for (int32_t i = static_cast<int32_t>(max_length - needle_length); i >= 0; --i) {

            if (std::memcmp(data + i, needle, needle_length) == 0) {
                return i;
            }
        }

        ESP_LOGD(PS_PTR_TAG, "last_special_index_of: Needle not found within %lu bytes", max_length);
        return -1;
    }

    // Überladung für nullterminierte Suchmuster, die Länge wird selbst bestimmt
    template <typename U = T>
        requires std::is_same_v<U, char>
    int32_t last_special_index_of(const char* needle, uint32_t max_length) const {
        return last_special_index_of(needle, needle ? std::strlen(needle) : 0, max_length);
    }

    // Sucht eine Teilzeichenkette ohne Beachtung der Groß-/Kleinschreibung, zum Beispiel findet index_of_icase("MP3") auch "mp3"
    template <typename U = T>
        requires std::is_same_v<U, char>
    int index_of_icase(const char* substr, std::size_t start = 0) const {
        if (!mem || !substr || !*substr)
            return -1;

        const char* str = static_cast<const char*>(mem.get());
        std::size_t len_str = std::strlen(str);
        std::size_t len_sub = std::strlen(substr);
        if (start >= len_str || len_sub == 0 || len_sub > len_str)
            return -1;

        for (std::size_t i = start; i <= len_str - len_sub; ++i) {
            bool match = true;
            for (std::size_t j = 0; j < len_sub; ++j) {
                if (std::tolower(str[i + j]) != std::tolower(substr[j])) {
                    match = false;
                    break;
                }
            }
            if (match)
                return static_cast<int>(i);
        }
        return -1;
    }
    // Sucht ein einzelnes Zeichen rückwärts, ohne Startangabe vom Ende her
    template <typename U = T>
        requires std::is_same_v<U, char>
    int last_index_of(char ch, int start_pos = -1) const {
        if (!mem)
            return -1;

        const char* str = static_cast<const char*>(mem.get());
        int len = static_cast<int>(std::strlen(str));

        // ohne Startangabe am Ende beginnen
        if (start_pos < 0 || start_pos >= len)
            start_pos = len - 1;

        for (int i = start_pos; i >= 0; --i) {
            if (str[i] == ch)
                return i;
        }
        return -1;
    }

    // Fassung für const char als Elementtyp
    template <typename U = T>
        requires std::is_same_v<U, const char>
    int last_index_of(const char ch, int start_pos = -1) const {
        if (!mem)
            return -1;

        const char* str = static_cast<const char*>(mem.get());
        int len = static_cast<int>(std::strlen(str));

        // ohne Startangabe am Ende beginnen
        if (start_pos < 0 || start_pos >= len)
            start_pos = len - 1;

        for (int i = start_pos; i >= 0; --i) {
            if (str[i] == ch)
                return i;
        }
        return -1;
    }

    // Sucht eine Teilzeichenkette rückwärts, ohne Startangabe vom Ende her
    template <typename U = T>
        requires std::is_same_v<U, char>
    int last_index_of(const char* substr, int start_pos = -1) const {
        if (!mem || !substr || !*substr)
            return -1;

        const char* str = static_cast<const char*>(mem.get());

        int len = static_cast<int>(std::strlen(str));
        int sub_len = static_cast<int>(std::strlen(substr));

        if (sub_len > len)
            return -1;

        // ohne Startangabe am Ende beginnen
        if (start_pos < 0 || start_pos > len - sub_len) {
            start_pos = len - sub_len;
        }

        for (int i = start_pos; i >= 0; --i) {
            if (std::strncmp(str + i, substr, sub_len) == 0) {
                return i;
            }
        }
        return -1;
    }

    // Sucht eine Teilzeichenkette in den ersten max_pos Zeichen
    template <typename U = T>
        requires std::is_same_v<U, char>
    int index_of_substr(const char* needle, std::size_t max_pos = SIZE_MAX) const {
        if (!mem || !needle || !*needle)
            return -1;

        const char* haystack = static_cast<const char*>(mem.get());
        std::size_t hay_len = std::min(std::strlen(haystack), max_pos);
        std::size_t needle_len = std::strlen(needle);

        if (needle_len > hay_len)
            return -1;

        for (std::size_t i = 0; i <= hay_len - needle_len; ++i) {
            if (std::memcmp(haystack + i, needle, needle_len) == 0) {
                return static_cast<int>(i);
            }
        }

        return -1;
    }
    // ===================================================================
    // Umwandeln und Zerlegen
    // ===================================================================

    // Liefert eine Kopie des Ausschnitts ab pos mit höchstens count Zeichen
    ps_ptr<char> substr(size_t pos, size_t count = std::string::npos) const {
        const char* src = mem.get();
        if (!src)
            return ps_ptr<char>{};

        size_t len = std::strlen(src);
        if (pos >= len)
            return ps_ptr<char>{}; // leer zurück

        size_t n = (count == std::string::npos || pos + count > len) ? (len - pos) : count;

        return ps_ptr<char>(src + pos, n);
    }
    // Wandelt am Platz in Kleinbuchstaben, nur für ASCII und ISO-8859-1
    template <typename U = T>
        requires std::is_same_v<U, char>
    void toLowerCase() {
        if (!mem)
            return;

        char* str = get();
        if (!str)
            return;

        for (; *str; ++str) {
            *str = static_cast<char>(std::tolower(static_cast<unsigned char>(*str)));
        }
    }

    // Wandelt am Platz in Großbuchstaben, nur für ASCII und ISO-8859-1
    template <typename U = T>
        requires std::is_same_v<U, char>
    void toUpperCase() {
        if (!mem)
            return;

        char* str = get();
        if (!str)
            return;

        for (; *str; ++str) {
            *str = static_cast<char>(std::toupper(static_cast<unsigned char>(*str)));
        }
    }

    // Länge in Bytes ohne die Nullterminierung
    template <typename U = T>
        requires std::is_same_v<U, char>
    size_t strlen() const {
        if (!valid())
            return 0;
        return std::strlen(get());
    }

    // Zählt sichtbare UTF-8-Zeichen und überspringt ANSI-Escape-Sequenzen
    size_t utf8_strlen() const {
        if (!get())
            return 0;

        size_t count = 0;
        const unsigned char* s = reinterpret_cast<const unsigned char*>(get());

        while (*s) {
            // ANSI-Escape-Sequenzen beginnen mit ESC [
            if (*s == 0x1B && s[1] == '[') {
                s += 2;
                while (*s && !(*s >= '@' && *s <= '~')) {
                    ++s; // Parameterzeichen (Ziffern, Semikola usw.)
                }
                if (*s)
                    ++s; // Schlussbuchstabe, zum Beispiel 'm'
                continue;
            }

            // UTF-8-Zeichen anhand des Startbytes abschreiten
            if ((*s & 0x80) == 0)
                s += 1; // ASCII
            else if ((*s & 0xE0) == 0xC0)
                s += 2; // 2 Bytes
            else if ((*s & 0xF0) == 0xE0)
                s += 3; // 3 Bytes
            else if ((*s & 0xF8) == 0xF0)
                s += 4; // 4 Bytes
            else
                s += 1; // ungültiges Startbyte, vorsichtig weiter

            ++count;
        }

        return count;
    }

    // Prüft, ob der Inhalt gültiges UTF-8 ist
    bool is_utf8() const {
        const unsigned char* s = reinterpret_cast<const unsigned char*>(mem.get());
        if (!s)
            return false;

        while (*s) {
            unsigned char c = *s++;

            // Ein Byte, ASCII (0xxxxxxx)
            if (c < 0x80)
                continue;

            // Zwei-Byte-Folge (110xxxxx 10xxxxxx)
            if ((c >> 5) == 0x6) {
                if ((s[0] & 0xC0) != 0x80)
                    return false;
                s += 1;
                continue;
            }

            // Drei-Byte-Folge (1110xxxx 10xxxxxx 10xxxxxx)
            if ((c >> 4) == 0xE) {
                if ((s[0] & 0xC0) != 0x80 || (s[1] & 0xC0) != 0x80)
                    return false;

                unsigned int cp = ((c & 0x0F) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F);
                // überlange Kodierung oder Surrogat?
                if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))
                    return false;

                s += 2;
                continue;
            }

            // Vier-Byte-Folge (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
            if ((c >> 3) == 0x1E) {
                if ((s[0] & 0xC0) != 0x80 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80)
                    return false;

                unsigned int cp = ((c & 0x07) << 18) | ((s[0] & 0x3F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
                // überlang oder außerhalb des Unicode-Bereichs
                if (cp < 0x10000 || cp > 0x10FFFF)
                    return false;

                s += 3;
                continue;
            }

            // ungültiges Startbyte
            return false;
        }

        return true;
    }

    // Grobe Strukturprüfung, ob der Inhalt ein JSON-Objekt ist
    bool isJson() const {
        if (!mem || allocated_size < 2)
            return false;

        const char* p = static_cast<const char*>(mem.get());

        // führenden Leerraum überspringen
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;

        if (*p != '{')
            return false;

        // Leerraum am Ende ignorieren
        const char* end = mem.get() + std::strlen(mem.get()) - 1;
        while (end > p && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
            end--;

        if (*end != '}')
            return false;

        bool in_string = false;
        int brace_count = 0;
        const char* curr = p;

        while (curr <= end) {
            if (!in_string) {
                if (*curr == '{')
                    brace_count++;
                else if (*curr == '}')
                    brace_count--;
                else if (*curr == '"')
                    in_string = true;
                else if (*curr == ':' || *curr == ',') {
                    // Doppelpunkte und Kommata sind außerhalb von Zeichenketten erlaubt
                } else if (*curr != ' ' && *curr != '\t' && *curr != '\n' && *curr != '\r') {
                    // vereinfachte Prüfung: nur Zeichen aus Zahlen, true, false, null und Klammern zulassen
                    if (!(*curr >= '0' && *curr <= '9') && *curr != '-' && *curr != '.' && *curr != 't' && *curr != 'f' && *curr != 'n' && *curr != '[' && *curr != ']') {
                        return false;
                    }
                }
            } else {
                if (*curr == '"')
                    in_string = false;
                else if (*curr == '\\')
                    curr++; // maskiertes Zeichen überspringen
            }
            curr++;

            if (brace_count < 0)
                return false; // schließende Klammer ohne Gegenstück
        }

        return brace_count == 0; // alle Klammern müssen sich schließen
    }

    // Wandelt \uXXXX-Escape-Folgen in UTF-8, zum Beispiel wird "\\u041f\\u0440..." zu kyrillischem Text
    void unicodeToUTF8(const char* src) {
        this->clear();
        if (!src)
            return;

        auto encodeCodepointToUTF8 = [](uint32_t cp, char* out) -> int {
            if (cp <= 0x7F) {
                out[0] = cp;
                return 1;
            } else if (cp <= 0x7FF) {
                out[0] = 0xC0 | (cp >> 6);
                out[1] = 0x80 | (cp & 0x3F);
                return 2;
            } else if (cp <= 0xFFFF) {
                out[0] = 0xE0 | (cp >> 12);
                out[1] = 0x80 | ((cp >> 6) & 0x3F);
                out[2] = 0x80 | (cp & 0x3F);
                return 3;
            } else if (cp <= 0x10FFFF) {
                out[0] = 0xF0 | (cp >> 18);
                out[1] = 0x80 | ((cp >> 12) & 0x3F);
                out[2] = 0x80 | ((cp >> 6) & 0x3F);
                out[3] = 0x80 | (cp & 0x3F);
                return 4;
            }
            return 0;
        };

        const char* ptr = src;
        char utf8[5];

        while (*ptr) {
            if (ptr[0] == '\\' && ptr[1] == 'u') {
                uint32_t codepoint = 0;
                if (sscanf(ptr + 2, "%4lx", &codepoint) == 1) {
                    int len = encodeCodepointToUTF8(codepoint, utf8);
                    utf8[len] = 0;
                    this->append(utf8);
                    ptr += 6; // überspringe \uXXXX
                } else {
                    this->append(ptr, 1);
                    ptr++;
                }
            } else {
                this->append(ptr, 1);
                ptr++;
            }
        }
    }
    // Ersetzt benannte und numerische HTML-Entities durch ihre UTF-8-Zeichen
    void htmlToUTF8(const char* src) {
        this->clear();
        if (!src)
            return;

        struct EntityMap {
            const char* name;
            uint32_t codepoint;
        };

        static const EntityMap entities[] = {
            {"amp", 0x0026},    // &
            {"lt", 0x003C},     // <
            {"gt", 0x003E},     // >
            {"quot", 0x0022},   // "
            {"apos", 0x0027},   // '
            {"nbsp", 0x00A0},   // geschütztes Leerzeichen
            {"euro", 0x20AC},   // Eurozeichen
            {"copy", 0x00A9},   // Copyright-Zeichen
            {"reg", 0x00AE},    // Marken-Zeichen
            {"trade", 0x2122},  // Trademark-Zeichen
            {"hellip", 0x2026}, // Auslassungspunkte
            {"ndash", 0x2013},  // Halbgeviertstrich
            {"mdash", 0x2014},  // Geviertstrich
            {"sect", 0x00A7},   // Paragrafenzeichen
            {"para", 0x00B6}    // Absatzzeichen
        };

        auto encodeCodepointToUTF8 = [](uint32_t cp, char* out) -> int {
            if (cp <= 0x7F) {
                out[0] = (char)cp;
                return 1;
            } else if (cp <= 0x7FF) {
                out[0] = (char)(0xC0 | (cp >> 6));
                out[1] = (char)(0x80 | (cp & 0x3F));
                return 2;
            } else if (cp <= 0xFFFF) {
                out[0] = (char)(0xE0 | (cp >> 12));
                out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out[2] = (char)(0x80 | (cp & 0x3F));
                return 3;
            } else if (cp <= 0x10FFFF) {
                out[0] = (char)(0xF0 | (cp >> 18));
                out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out[3] = (char)(0x80 | (cp & 0x3F));
                return 4;
            }
            return 0;
        };

        auto find_named_entity = [&](const char* p, uint32_t* cp_out, int* entity_len) -> bool {
            for (size_t i = 0; i < sizeof(entities) / sizeof(entities[0]); ++i) {
                const char* name = entities[i].name;
                size_t len = std::strlen(name);
                if (strncmp(p + 1, name, len) == 0 && p[1 + len] == ';') {
                    *cp_out = entities[i].codepoint;
                    *entity_len = (int)(len + 2); // &name;
                    return true;
                }
            }
            return false;
        };

        const char* p = src;
        char utf8[5];

        while (*p) {
            if (p[0] == '&') {
                uint32_t cp = 0;
                int ent_len = 0;

                // benannte Entity
                if (find_named_entity(p, &cp, &ent_len)) {
                    int n = encodeCodepointToUTF8(cp, utf8);
                    if (n > 0) {
                        utf8[n] = '\0';
                        this->append(utf8);
                        p += ent_len;
                        continue;
                    }
                }

                // numerische Entity
                if (p[1] == '#') {
                    const char* q = p + 2;
                    uint32_t value = 0;

                    if (*q == 'x' || *q == 'X') {
                        // hexadezimal, etwa &#x1F60A;
                        q++;
                        char* endptr = nullptr;
                        unsigned long tmp = strtoul(q, &endptr, 16);
                        if (endptr && *endptr == ';' && tmp <= 0x10FFFF) {
                            value = (uint32_t)tmp;
                            int n = encodeCodepointToUTF8(value, utf8);
                            if (n > 0) {
                                utf8[n] = '\0';
                                this->append(utf8);
                                p = endptr + 1;
                                continue;
                            }
                        }
                    } else {
                        // dezimal, etwa &#1234;
                        char* endptr = nullptr;
                        unsigned long tmp = strtoul(q, &endptr, 10);
                        if (endptr && *endptr == ';' && tmp <= 0x10FFFF) {
                            value = (uint32_t)tmp;
                            int n = encodeCodepointToUTF8(value, utf8);
                            if (n > 0) {
                                utf8[n] = '\0';
                                this->append(utf8);
                                p = endptr + 1;
                                continue;
                            }
                        }
                    }
                }

                // unbekannte Entity: nur das '&' übernehmen
                this->append(p, 1);
                p++;
                continue;
            }

            // normales Zeichen
            this->append(p, 1);
            p++;
        }
    }

    // ===================================================================
    // Zugriff und Besitz
    // ===================================================================

    // Roher Zeiger auf den gehaltenen Speicher, nullptr ohne Allokation
    T* get() const {
        return static_cast<T*>(mem.get());
    }

    // Wie get(), liefert aber nie nullptr, sondern den Ersatztext
    const char* c_get(const char* fallback = "") const {
        if constexpr (std::is_same_v<T, char>) {
            return mem ? mem.get() : fallback;
        } else {
            return fallback;
        }
    }

    // Gibt Name und Inhalt auf der Standardausgabe aus
    template <typename U = T>
        requires std::is_same_v<U, char>
    void print() const {
        printf("%s: %s", name ? name : "unnamed", c_get());
    }

    // Gibt Name und Inhalt mit Zeilenumbruch auf der Standardausgabe aus
    template <typename U = T>
        requires std::is_same_v<U, char>
    void println() const {
        printf("%s: %s\n", name ? name : "unnamed", c_get());
    }

    // Übernimmt den Besitz eines fremden Zeigers samt Größenangabe, freigegeben wird er später mit free()
    void set(T* ptr, std::size_t size = 0) {
        if (mem.get() != ptr) {
            mem.reset(ptr);
            allocated_size = size;
        }
    }

    // Elementzugriff für verschachtelte ps_ptr<ps_ptr<T>>
    template <typename U = T> auto at(size_t index) -> typename std::enable_if<std::is_same<U, ps_ptr<typename U::element_type>>::value, ps_ptr<typename U::element_type>&>::type {
        return static_cast<ps_ptr<typename U::element_type>*>(get())[index];
    }
    using element_type = T;

    // Deutet den gehaltenen Speicher als anderen Typ, zum Beispiel generic.as<uint32_t>()
    template <typename U> U* as() const {
        return reinterpret_cast<U*>(get());
    }

    // Fügt die Zeichenkette an der Position ein, eine Position hinter dem Ende hängt an
    bool insert(const char* insertStr, std::size_t pos) {
        if (!insertStr || !valid())
            return false;

        std::size_t originalLen = std::strlen(get());
        std::size_t insertLen = std::strlen(insertStr);

        if (pos > originalLen)
            pos = originalLen;

        std::size_t newLen = originalLen + insertLen + 1;
        ps_ptr<char> temp;
        temp.alloc(newLen);
        if (!temp.valid())
            return false;

        char* dst = temp.get();

        // bis zur Einfügeposition kopieren, dann den neuen Text, dann den Rest samt Nullterminierung
        std::memcpy(dst, get(), pos);
        std::memcpy(dst + pos, insertStr, insertLen);
        std::memcpy(dst + pos + insertLen, get() + pos, originalLen - pos + 1);

        this->assign(temp.get());
        return true;
    }

    // Verkleinert den Puffer auf die tatsächliche Länge plus Nullterminierung
    template <typename U = T>
        requires std::is_same_v<U, char>
    void shrink_to_fit() {
        if (!mem)
            return;
        std::size_t len = std::strlen(get());
        if (len == 0)
            return;
        ps_ptr<char> temp;
        temp.alloc(len + 1);
        if (!temp.valid())
            return;
        std::memcpy(temp.get(), get(), len + 1); // inklusive \0
        this->assign(temp.get());
    }
    // ===================================================================
    // Zahlwandlung
    // ===================================================================

    // Liest den Inhalt als Zahl zur angegebenen Basis, leer oder ungültig liefert 0 mit Fehlermeldung
    template <typename U = T>
        requires std::is_same_v<U, char>
    uint64_t to_uint64(int base = 10) const {
        static_assert(std::is_same_v<T, char>, "to_uint64 is only valid for ps_ptr<char>");
        if (!mem || !get()) {
            ESP_LOGE(PS_PTR_TAG, "to_uint64: No valid string data");
            return 0;
        }
        const char* str = get();
        char* end = nullptr;
        uint64_t result = std::strtoull(str, &end, base);
        if (end == str) {
            ESP_LOGE(PS_PTR_TAG, "to_uint64: Invalid numeric value in '%s' for base %d", str, base);
            return 0;
        }
        return result;
    }
    // Wie to_uint64(), zusätzlich liefert ein Wert über UINT32_MAX 0 mit Fehlermeldung
    uint32_t to_uint32(int base = 10) const {
        static_assert(std::is_same_v<T, char>, "to_uint32 is only valid for ps_ptr<char>");
        if (!mem || !get()) {
            ESP_LOGE(PS_PTR_TAG, "to_uint32: No valid string data");
            return 0;
        }
        const char* str = get();
        char* end = nullptr;
        unsigned long result = std::strtoul(str, &end, base);
        if (end == str) {
            ESP_LOGE(PS_PTR_TAG, "to_uint32: Invalid numeric value in '%s' for base %i", str, base);
            return 0;
        }
        if (result > UINT32_MAX) {
            ESP_LOGE(PS_PTR_TAG, "to_uint32: Value in '%s' exceeds UINT32_MAX (%u) for base %i", str, UINT32_MAX, base);
            return 0;
        }
        return static_cast<uint32_t>(result);
    }
    // Liest den Inhalt als vorzeichenbehaftete Zahl. Leer, ungültig oder außerhalb des int32-Bereichs liefert 0 mit Fehlermeldung
    int32_t to_int32(int base = 10) const {
        static_assert(std::is_same_v<T, char>, "to_int32 is only valid for ps_ptr<char>");
        if (!mem || !get()) {
            ESP_LOGE(PS_PTR_TAG, "to_int32: No valid string data");
            return 0;
        }

        const char* str = get();
        char* end = nullptr;
        long result = std::strtol(str, &end, base);
        if (result == 0 && errno == EINVAL) {
            ESP_LOGE(PS_PTR_TAG, "invalid content %s", str);
            return 0;
        }

        if (end == str) {
            ESP_LOGE(PS_PTR_TAG, "to_int32: Invalid numeric value in '%s' for base %i", str, base);
            return 0;
        }

        if (result < INT32_MIN || result > INT32_MAX) {
            ESP_LOGE(PS_PTR_TAG, "to_int32: Value in '%s' exceeds INT32 range (%ld..%ld) for base %i", str, (long)INT32_MIN, (long)INT32_MAX, base);
            return 0;
        }

        return static_cast<int32_t>(result);
    }
    // Wie to_int32(), aber für den int64-Bereich
    int64_t to_int64(int base = 10) const {
        static_assert(std::is_same_v<T, char>, "to_int64 is only valid for ps_ptr<char>");
        if (!mem || !get()) {
            ESP_LOGE(PS_PTR_TAG, "to_int64: No valid string data");
            return 0;
        }

        const char* str = get();
        char* end = nullptr;
        long long result = std::strtoll(str, &end, base);

        if (end == str) {
            ESP_LOGE(PS_PTR_TAG, "to_int64: Invalid numeric value in '%s' for base %i", str, base);
            return 0;
        }

        if (result < INT64_MIN || result > INT64_MAX) {
            ESP_LOGE(PS_PTR_TAG, "to_int64: Value in '%s' exceeds INT64 range (%lld..%lld) for base %i", str, (long long)INT64_MIN, (long long)INT64_MAX, base);
            return 0;
        }

        return static_cast<int64_t>(result);
    }
    // Liest bis zu 8 Bytes in Big-Endian-Reihenfolge und speichert den Wert als Hex-Zeichenkette, aus {0x12, 0x34, 0x56, 0x78} wird "0x12345678". Ohne Daten wird "0x0" gespeichert
    template <typename U = T>
        requires std::is_same_v<U, char>
    void big_endian(const uint8_t* data, uint8_t size) {
        static_assert(std::is_same_v<T, char>, "big_endian is only valid for ps_ptr<char>");
        if (!data || size == 0) {
            ESP_LOGE(PS_PTR_TAG, "big_endian: Invalid input (data is null or size is 0)");
            assign("0x0");
            return;
        }
        if (size > 8) {
            ESP_LOGE(PS_PTR_TAG, "big_endian: Size %u exceeds 8 bytes, truncating to 8", size);
            size = 8;
        }
        uint64_t result = 0;
        for (uint8_t i = 0; i < size; ++i) {
            result = (result << 8) | data[i];
        }
        char buffer[19]; // "0x" + 16 Hex-Ziffern + Nullterminierung
        snprintf(buffer, sizeof(buffer), "0x%llx", result);
        assign(buffer);
    }

    // Wie big_endian(), liest aber in Little-Endian-Reihenfolge, aus {0x78, 0x56, 0x34, 0x12} wird "0x12345678"
    template <typename U = T>
        requires std::is_same_v<U, char>
    void little_endian(const uint8_t* data, uint8_t size) {
        static_assert(std::is_same_v<T, char>, "little_endian is only valid for ps_ptr<char>");
        if (!data || size == 0) {
            ESP_LOGE(PS_PTR_TAG, "little_endian: Invalid input (data is null or size is 0)");
            assign("0x0");
            return;
        }
        if (size > 8) {
            ESP_LOGE(PS_PTR_TAG, "little_endian: Size %u exceeds 8 bytes, truncating to 8", size);
            size = 8;
        }
        uint64_t result = 0;
        for (int i = size - 1; i >= 0; --i) {
            result = (result << 8) | data[i];
        }
        char buffer[19]; // "0x" + 16 Hex-Ziffern + Nullterminierung
        snprintf(buffer, sizeof(buffer), "0x%llx", result);
        assign(buffer);
    }

    // Entfernt alle aufgezählten Zeichen am Platz, remove_chars("[]:.") macht aus "[00:12.45]" die Folge "001245"
    void remove_chars(const char* chars) {
        if (!valid() || !chars)
            return;
        char* n = name;
        char* dst = get();
        char* src = get();

        while (*src) {
            if (!std::strchr(chars, *src)) {
                *dst++ = *src;
            }
            ++src;
        }
        *dst = '\0';
        name = n;
    }
    // Ersetzt jedes Vorkommen von from durch to, aus "/user/temp/file.tmp" wird mit replace("temp", "music") der Pfad "/user/music/file.tmp"
    bool replace(const char* from, const char* to) {
        if (!valid() || !from || !*from || !to)
            return false;

        const char* src = get();
        std::size_t fromLen = std::strlen(from);
        std::size_t toLen = std::strlen(to);

        if (fromLen == 0)
            return false; // nichts zu ersetzen

        std::vector<char> result;
        const char* read = src;

        while (*read) {
            if (std::strncmp(read, from, fromLen) == 0) {
                result.insert(result.end(), to, to + toLen);
                read += fromLen;
            } else {
                result.push_back(*read++);
            }
        }

        result.push_back('\0');

        this->copy_from(result.data(), result.size() - 1);
        return true;
    }

    // Entfernt Leerraum an Anfang und Ende am Platz
    template <typename U = T>
        requires std::is_same_v<U, char>
    void trim() {
        if (!mem)
            return;

        char* str = static_cast<char*>(mem.get());
        std::size_t len = std::strlen(str);
        if (len == 0)
            return;

        // trim on the left
        char* start = str;
        while (*start && isspace(*start))
            ++start;

        // trim on the right
        char* end = str + len - 1;
        while (end >= start && isspace(*end))
            --end;
        *(end + 1) = '\0';

        // wenn der Anfang verschoben ist, alles nach vorne kopieren
        if (start != str) {
            std::size_t new_len = end - start + 1;
            std::memmove(str, start, new_len + 1);
        }
    }

    // Schneidet am ersten Vorkommen des Zeichens ab, truncate_at(',') macht aus "Hello, World" den Text "Hello"
    void truncate_at(char ch) {
        if (!valid())
            return;
        char* str = get();
        char* pos = strchr(str, ch);
        if (pos)
            *pos = '\0';
    }

    // Schneidet an der Position ab, eine Position hinter dem Ende bleibt wirkungslos
    void truncate_at(std::size_t pos) {
        if (!valid())
            return;
        if (pos >= strlen())
            return;
        char* str = get();
        std::size_t len = std::strlen(str);
        if (pos < len)
            str[pos] = '\0';
        else
            ESP_LOGE(PS_PTR_TAG, "truncate pos %i out of length %i", pos, len);
    }

    // Entfernt das Präfix am Anfang, remove_prefix("../../") macht aus "../../a/b.m3u8" den Pfad "a/b.m3u8"
    void remove_prefix(const char* prefix) {
        if (!valid() || !prefix)
            return;

        std::size_t prefix_len = std::strlen(prefix);
        if (std::strncmp(get(), prefix, prefix_len) == 0) {
            char* str = get();
            std::size_t len = std::strlen(str);
            std::memmove(str, str + prefix_len, len - prefix_len + 1); // inkl. '\0'
        }
    }
    // Entfernt alles bis zum ersten Vorkommen des Zeichens, mit includeChar auch das Zeichen selbst
    void remove_before(char ch, bool includeChar = true) {
        if (!valid())
            return;

        char* str = get();
        char* pos = strchr(str, ch);
        if (pos) {
            if (!includeChar)
                ++pos; // wenn nicht inklusive: das Zeichen behalten
            std::size_t remaining = std::strlen(pos);
            std::memmove(str, pos, remaining + 1); // inkl. '\0'
        }
    }
    // Entfernt alles vor dem Index, mit includeIdx auch das Zeichen an dieser Stelle
    void remove_before(int idx, bool includeIdx = true) {
        if (!valid())
            return;

        char* str = get();
        std::size_t len = std::strlen(str);

        if (idx < 0 || static_cast<std::size_t>(idx) > len)
            return;

        char* pos = str + idx;
        if (!includeIdx && idx < len)
            ++pos; // wenn das Zeichen nicht entfernt werden soll

        std::size_t remaining = std::strlen(pos);
        std::memmove(str, pos, remaining + 1); // inkl. '\0'
    }
    // Schiebt den Pufferinhalt um n Bytes nach links und füllt das Ende mit Nullen auf
    void shift_left(int n) {
        if (!mem || allocated_size == 0) {
            printf("Error: No allocated memory or invalid buffer\n");
            return;
        }

        if (n < 0 || static_cast<std::size_t>(n) > allocated_size) {
            printf("Error: Invalid shift amount %d, allocated_size=%zu\n", n, allocated_size);
            return;
        }

        char* str = mem.get();
        if (n == 0)
            return;

        std::size_t remaining = allocated_size - n;
        std::memmove(str, str + n, remaining);

        std::memset(str + remaining, 0, n);
    }

    // Kommt die Teilzeichenkette im Inhalt vor?
    bool contains(const char* substr) const {
        return substr && this->valid() && std::strstr(this->get(), substr);
    }

    // Wie contains(), aber ohne Beachtung der Groß-/Kleinschreibung
    bool contains_with_icase(const char* substr) const {
        if (!substr || !this->valid()) {
            return false;
        }

        const char* haystack = this->get();
        const char* needle = substr;

        // beide Texte in Kleinbuchstaben vergleichen
        std::string haystack_lower(haystack);
        std::string needle_lower(needle);

        std::transform(haystack_lower.begin(), haystack_lower.end(), haystack_lower.begin(), [](unsigned char c) {
            return std::tolower(c);
        });
        std::transform(needle_lower.begin(), needle_lower.end(), needle_lower.begin(), [](unsigned char c) {
            return std::tolower(c);
        });

        return haystack_lower.find(needle_lower) != std::string::npos;
    }
    // Setzt den gesamten allokierten Block auf null, der Speicher bleibt allokiert
    void clear() {
        if (mem && allocated_size > 0) {
            std::memset(mem.get(), 0, allocated_size);
            length_ = 0;
        }
    }

    // Allokierte Größe in Bytes
    size_t size() const {
        return allocated_size;
    }

    // Ist nichts allokiert?
    bool empty() const noexcept {
        return allocated_size == 0;
    }

    // Hält das Objekt Speicher?
    bool valid() const {
        return mem != nullptr;
    }

    // Gibt den Speicher frei und setzt die Größe auf null
    void reset() {
        mem.reset();
        allocated_size = 0;
    }

    // Gibt die ersten n Bytes als Hexdump mit ASCII-Zeile aus
    void hex_dump(uint16_t n = UINT16_MAX) {
        if (!valid()) {
            printf("hex_dump: invalid buffer\n");
            return;
        }

        if (allocated_size < n)
            n = allocated_size;
        if (n == 0) {
            printf("hex_dump: no data\n");
            return;
        }

        uint8_t items_per_line = 30;

        static const char* sym[32] = {"NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL", "BS ", "TAB", "LF ", "VT ", "FF ", "CR ", "SO ", "SI ",
                                      "DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB", "CAN", "EM ", "SUB", "ESC", "FS ", "GS ", "RS ", "US "};

        const uint8_t* buff = reinterpret_cast<const uint8_t*>(get());
        if (!name)
            printf("dumping %u bytes:\n", n);
        else
            printf("%s dumping %u bytes:\n", name, n);

        for (uint16_t i = 0; i < n; i += items_per_line) {
            uint16_t m = std::min<uint16_t>(n, i + items_per_line);
            uint8_t s = 0;

            // Hex view
            for (uint16_t j = i; j < m; j++) {
                printf("0x%02X ", buff[j]);
                if (++s % 10 == 0)
                    printf("   ");
            }
            printf("\n");

            // ASCII / symbolic view
            s = 0;
            for (uint16_t j = i; j < m; j++) {
                uint8_t c = buff[j];
                if (c >= 32)
                    printf("%c    ", c);
                else
                    printf("%s  ", sym[c]);
                if (++s % 10 == 0)
                    printf("   ");
            }
            printf("\n");
        }
        printf("\n");
    }
    // ===================================================================
    // Operatoren
    // ===================================================================

    // Kopierzuweisung, tiefe Kopie, nur für ps_ptr<char> zulässig
    ps_ptr& operator=(const ps_ptr& other) {
        if (this != &other) {
            if constexpr (std::is_same_v<T, char>) {
                assign(other.get());
            } else {
                static_assert(!std::is_same_v<T, T>, "Copy assignment disabled for this type");
            }
        }
        return *this;
    }

    // Zuweisung einer Zeichenkette, zum Beispiel h = "123", nur für ps_ptr<char> zulässig
    ps_ptr<T>& operator=(const T* raw_ptr) {
        if constexpr (std::is_same_v<T, char>) {
            assign(raw_ptr);
        } else {
            static_assert(!std::is_same_v<T, T>, "Assignment from const pointer disabled for this type");
        }
        return *this;
    }

    // Übernimmt den Besitz eines rohen Zeigers, die Größe ist danach unbekannt und steht auf 0
    ps_ptr<T>& operator=(T* raw_ptr) {
        if (mem.get() != raw_ptr) {
            mem.reset(raw_ptr);
            allocated_size = 0;
        }
        return *this;
    }

    ps_ptr& operator=(ps_ptr&& other) noexcept {
        if (this != &other) {
            if (name) {
                free(name);
                name = nullptr;
            }
            mem = std::move(other.mem);
            allocated_size = other.allocated_size;
            name = other.name;
            other.allocated_size = 0;
            other.name = nullptr;
        }
        return *this;
    }

    T* operator->() const {
        return get();
    }
    T& operator*() const {
        return *get();
    }

    // Geprüfter Elementzugriff, ein Index außerhalb der Allokation trifft auf ein Ersatzobjekt und wird protokolliert
    T& operator[](std::size_t index) noexcept {
        if (index >= allocated_size) {
            ESP_LOGE(PS_PTR_TAG, "[%s:%i] ps_ptr[]: Index %zu out of bounds (size = %zu, name = %s)", __FILE__, __LINE__, index, allocated_size, name ? name : "unnamed");
            return dummy;
        }
        return mem[index];
    }

    const T& operator[](std::size_t index) const noexcept {
        if (index >= allocated_size) {
            ESP_LOGE(PS_PTR_TAG, "[%s:%i] ps_ptr[]: Index %zu out of bounds (size = %zu, name = %s)", __FILE__, __LINE__, index, allocated_size, name ? name : "unnamed");
            return dummy;
        }
        return mem[index];
    }

    // Dreiwegevergleich: bei char nach Inhalt, sonst nach Zeigeradresse
    auto operator<=>(const ps_ptr& other) const noexcept {
        if constexpr (std::is_same_v<T, char>) {
            const char* s1 = get();
            const char* s2 = other.get();
            int result = std::strcmp(s1 ? s1 : "", s2 ? s2 : "");
            return (result < 0) ? std::strong_ordering::less : (result > 0) ? std::strong_ordering::greater : std::strong_ordering::equal;
        } else {
            return mem.get() <=> other.mem.get();
        }
    }

    // Hängt eine Zeichenkette an, wie append()
    template <typename U = T>
        requires std::is_same_v<U, char>
    ps_ptr<char>& operator+=(const char* rhs) {
        append(rhs);
        return *this;
    }

    // Hängt den Inhalt eines anderen ps_ptr<char> an
    template <typename U = T>
        requires std::is_same_v<U, char>
    ps_ptr<char>& operator+=(const ps_ptr<char>& rhs) {
        append(rhs);
        return *this;
    }

    // Iteratoren über den allokierten Bereich
    T* begin() noexcept {
        return mem.get();
    }
    const T* begin() const noexcept {
        return mem.get();
    }

    T* end() noexcept {
        return mem.get() + allocated_size;
    }
    const T* end() const noexcept {
        return mem.get() + allocated_size;
    }

    const T* cbegin() const noexcept {
        return mem.get();
    }
    const T* cend() const noexcept {
        return mem.get() + allocated_size;
    }

    // Zeigerarithmetik auf den gehaltenen Speicher
    T* operator+(std::size_t offset) noexcept {
        return mem.get() + offset;
    }
    const T* operator+(std::size_t offset) const noexcept {
        return mem.get() + offset;
    }

    // Setzt jedes Element auf den Wert
    void fill(const T& value) {
        std::fill(begin(), end(), value);
    }

    // Sicht auf die Daten als std::span
    std::span<T> span() noexcept {
        return std::span<T>(mem.get(), allocated_size);
    }

    std::span<const T> span() const noexcept {
        return std::span<const T>(mem.get(), allocated_size);
    }

    // ===================================================================
    // Formatierung im {}-Stil
    // ===================================================================

    // Wandelt einen beliebigen Wert in Text, Grundlage der {}-Formatierung
    template <typename V> std::string to_string_any(const V& v) {
        using Raw = std::remove_cv_t<std::remove_reference_t<V>>;
        using D = std::decay_t<V>;

        // ps_ptr<char>
        if constexpr (std::is_same_v<Raw, ps_ptr<char>>) {
            const char* str = v.c_get();
            return str ? std::string(str) : "";
        }
        // char-Arrays wie String-Literale als Text behandeln
        else if constexpr (std::is_array_v<Raw> && std::is_same_v<std::remove_extent_t<Raw>, char>) {
            return std::string(v);
        }
        // bool
        else if constexpr (std::is_same_v<D, bool>) {
            return v ? "true" : "false";
        }
        // Gleitkomma
        else if constexpr (std::is_same_v<D, float> || std::is_same_v<D, double>) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%g", v);
            return buf;
        }
        // Ganzzahl und Enum
        else if constexpr (std::is_integral_v<D> || std::is_enum_v<D>) {
            return std::to_string(static_cast<long long>(v));
        }
        // Zeiger
        else if constexpr (std::is_pointer_v<D>) {
            // char*
            if constexpr (std::is_same_v<D, char*> || std::is_same_v<D, const char*>) {
                return v ? std::string(v) : "";
            }
            // unsigned char*
            else if constexpr (std::is_same_v<D, unsigned char*> || std::is_same_v<D, const unsigned char*>) {
                return v ? std::string(reinterpret_cast<const char*>(v)) : "";
            }

            // sonstige Zeiger als Adresse
            else {
                char buf[32];
                snprintf(buf, sizeof(buf), "%p", static_cast<const void*>(v));
                return buf;
            }
        }
        // Rückfall
        else {
            return v;
        }
    }

    void format_append(std::string& out, const char* fmt) {
        out += fmt;
    }

    template <typename First, typename... Rest> void format_append(std::string& out, const char* fmt, First&& first, Rest&&... rest) {
        while (*fmt) {

            // Start eines Formatfeldes?
            if (*fmt == '{') {

                // Escape {{
                if (fmt[1] == '{') {
                    out += '{';
                    fmt += 2;
                    continue;
                }

                // Ende suchen
                const char* end = std::strchr(fmt, '}');

                // Fehlerfall: keine schließende Klammer
                if (!end) {
                    out += *fmt++;
                    continue;
                }

                // Inhalt zwischen { ... }
                std::string spec_str(fmt + 1, end - fmt - 1);

                format_spec fs;

                // Nur parsen wenn Inhalt existiert
                // also {:02} usw.
                if (!spec_str.empty()) {
                    fs = parse_format(spec_str.c_str());
                }

                // Wert formatieren
                out += format_value(std::forward<First>(first), fs);

                // Rekursiv weitermachen
                format_append(out, end + 1, std::forward<Rest>(rest)...);

                return;
            }

            // Escape }}
            if (fmt[0] == '}' && fmt[1] == '}') {
                out += '}';
                fmt += 2;
                continue;
            }

            out += *fmt++;
        }
    }

    struct format_spec {
        int width = 0;
        int precision = -1;
        char fill = ' ';
        char type = 0;
        bool upper = false;
        bool align_right = false;
    };

    inline format_spec parse_format(const char* fmt) {
        format_spec fs;

        // {:02}
        if (*fmt == ':') {
            ++fmt;

            // rechtsbündig?
            if (*fmt == '-') {
                fs.align_right = true;
                ++fmt;
            }

            // führende Null
            if (*fmt == '0') {
                fs.fill = '0';
                ++fmt;
            }

            // Breite
            while (isdigit(*fmt)) {
                fs.width = fs.width * 10 + (*fmt - '0');
                ++fmt;
            }

            // Nachkommastellen
            if (*fmt == '.') {
                ++fmt;
                fs.precision = 0;
                while (isdigit(*fmt)) {
                    fs.precision = fs.precision * 10 + (*fmt - '0');
                    ++fmt;
                }
            }

            // Typkennzeichen
            if (*fmt) {
                fs.type = *fmt;
                if (*fmt == 'X')
                    fs.upper = true;
            }
        }
        return fs;
    }

    template <typename V> std::string format_value(const V& value, const format_spec& fs) {
        char buf[64];

        // bool ist in C++ ein Integraltyp, soll hier aber als Text ausgegeben werden.
        if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<V>>, bool>) {
            return value ? "true" : "false";
        }

        // ps_ptr<char>
        if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<V>>, ps_ptr<char>>) {
            const char* str = value.c_get();
            return str ? std::string(str) : std::string("");
        }

        // char braucht eine eigene Behandlung vor is_integral_v
        if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<V>>, char>) {
            if (fs.type == 'c' || fs.type == 0) { // 'c' oder kein Typkennzeichen
                buf[0] = value;
                buf[1] = '\0';
                return buf;
            }
        }

        if constexpr (std::is_integral_v<V>) {
            // hexadezimal in Großbuchstaben
            if (fs.type == 'X') {
                if (fs.width > 0) {
                    const int safe_width = (fs.width > 30) ? 30 : fs.width;
                    snprintf(buf, sizeof(buf), "%0*llX", safe_width, (unsigned long long)value);
                } else {
                    snprintf(buf, sizeof(buf), "%llX", (unsigned long long)value);
                }
                std::string s = buf;
                apply_width(s, fs, true);
                return s;
            }
            // hexadezimal in Kleinbuchstaben
            if (fs.type == 'x') {
                if (fs.width > 0) {
                    const int safe_width = (fs.width > 30) ? 30 : fs.width;
                    snprintf(buf, sizeof(buf), "%0*llx", safe_width, (unsigned long long)value);
                } else {
                    snprintf(buf, sizeof(buf), "%llx", (unsigned long long)value);
                }
                std::string s = buf;
                apply_width(s, fs, true);
                return s;
            }

            // Ganzzahl mit Auffüllung
            if (fs.width > 0) {
                const int safe_width = (fs.width > 30) ? 30 : fs.width;
                if (fs.fill == '0') {
                    snprintf(buf, sizeof(buf), "%0*lld", safe_width, (long long)value);
                } else {
                    snprintf(buf, sizeof(buf), "%*lld", safe_width, (long long)value);
                }
                std::string s = buf;
                apply_width(s, fs, true);
                return s;
            }
            std::string s = to_string_any(value);
            apply_width(s, fs, true);
            return s;
        }
        // Gleitkomma
        if constexpr (std::is_floating_point_v<V>) {
            if (fs.precision >= 0) {
                snprintf(buf, sizeof(buf), "%.*f", fs.precision, value);
                std::string s = buf;
                apply_width(s, fs, true);
                return s;
            }
            snprintf(buf, sizeof(buf), "%g", value);
            std::string s = buf;
            apply_width(s, fs, true);
            return s;
        }

        std::string s = to_string_any(value);
        apply_width(s, fs, false);
        return s;
    }

    inline size_t count_placeholders(const char* fmt) {
        size_t count = 0;

        while (*fmt) {
            // maskiertes {{
            if (fmt[0] == '{' && fmt[1] == '{') {
                fmt += 2;
                continue;
            }
            // maskiertes }}
            if (fmt[0] == '}' && fmt[1] == '}') {
                fmt += 2;
                continue;
            }

            // echter Platzhalter
            if (*fmt == '{') {
                const char* end = std::strchr(fmt, '}');

                if (end) {
                    ++count;
                    fmt = end + 1;
                    continue;
                }
            }
            ++fmt;
        }
        return count;
    }

    inline void apply_width(std::string& s, const format_spec& fs, bool numeric = false) {
        if (fs.width <= 0)
            return;

        if ((int)s.size() >= fs.width)
            return;
        size_t missing = fs.width - s.size();
        // Zahlen werden immer vorn aufgefüllt
        if (numeric) {
            s.insert(s.begin(), missing, fs.fill);
            return;
        }

        // Strings
        if (fs.align_right) {
            s.insert(s.begin(), missing, fs.fill);
        } else {
            s.append(missing, fs.fill);
        }
    }
};
// ===================================================================
// Freie Operatoren
// ===================================================================

// Verkettung zweier Zeichenketten in ein neues Objekt
template <typename T>
    requires std::is_same_v<T, char>
ps_ptr<char> operator+(const ps_ptr<T>& lhs, const ps_ptr<T>& rhs) {
    ps_ptr<char> result(lhs);
    result.append(rhs);
    return result;
}

template <typename T>
    requires std::is_same_v<T, char>
ps_ptr<char> operator+(const ps_ptr<T>& lhs, const char* rhs) {
    ps_ptr<char> result(lhs);
    result.append(rhs);
    return result;
}

template <typename T>
    requires std::is_same_v<T, char>
ps_ptr<char> operator+(const char* lhs, const ps_ptr<T>& rhs) {
    ps_ptr<char> result(lhs);
    result.append(rhs);
    return result;
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator==(const ps_ptr<T>& lhs, const ps_ptr<T>& rhs) {
    const char* a = lhs.get();
    const char* b = rhs.get();
    if (!a || !b)
        return a == b; // beide nullptr -> true
    return std::strcmp(a, b) == 0;
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator==(const ps_ptr<T>& lhs, const char* rhs) {
    const char* a = lhs.get();
    if (!a || !rhs)
        return a == rhs;
    return std::strcmp(a, rhs) == 0;
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator==(const char* lhs, const ps_ptr<T>& rhs) {
    return rhs == lhs;
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator!=(const ps_ptr<T>& lhs, const ps_ptr<T>& rhs) {
    return !(lhs == rhs);
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator!=(const ps_ptr<T>& lhs, const char* rhs) {
    return !(lhs == rhs);
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator!=(const char* lhs, const ps_ptr<T>& rhs) {
    return !(lhs == rhs);
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator<(const ps_ptr<T>& lhs, const ps_ptr<T>& rhs) {
    const char* a = lhs.get();
    const char* b = rhs.get();
    if (!a || !b)
        return a < b; // nullptr kleiner als nicht-null
    return std::strcmp(a, b) < 0;
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator<(const ps_ptr<T>& lhs, const char* rhs) {
    const char* a = lhs.get();
    if (!a || !rhs)
        return a < rhs;
    return std::strcmp(a, rhs) < 0;
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator<(const char* lhs, const ps_ptr<T>& rhs) {
    const char* b = rhs.get();
    if (!lhs || !b)
        return lhs < b;
    return std::strcmp(lhs, b) < 0;
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator>(const ps_ptr<T>& lhs, const ps_ptr<T>& rhs) {
    return rhs < lhs;
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator>(const ps_ptr<T>& lhs, const char* rhs) {
    return rhs && (std::strcmp(lhs.get(), rhs) > 0);
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator>(const char* lhs, const ps_ptr<T>& rhs) {
    return rhs < lhs;
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator<=(const ps_ptr<T>& lhs, const ps_ptr<T>& rhs) {
    return !(rhs < lhs);
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator<=(const ps_ptr<T>& lhs, const char* rhs) {
    return !(lhs > rhs);
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator<=(const char* lhs, const ps_ptr<T>& rhs) {
    return !(rhs < lhs);
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator>=(const ps_ptr<T>& lhs, const ps_ptr<T>& rhs) {
    return !(lhs < rhs);
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator>=(const ps_ptr<T>& lhs, const char* rhs) {
    return !(lhs < rhs);
}

template <typename T>
    requires std::is_same_v<T, char>
bool operator>=(const char* lhs, const ps_ptr<T>& rhs) {
    return !(lhs < rhs);
}

template <typename T>
    requires std::is_same_v<T, char>
std::ostream& operator<<(std::ostream& os, const ps_ptr<T>& str) {
    const char* s = str.get();
    if (s)
        os << s;
    return os;
}

// ===================================================================
// ps_struct_ptr
// ===================================================================

// Besitzender Zeiger auf ein einzelnes Struct im PSRAM. Verwendung:
//   ps_struct_ptr<hwoe_t> result;
//   result.alloc();
//   result->extension.copy_from(path);
template <typename T> class ps_struct_ptr {
  private:
    std::unique_ptr<T, PsramDeleter> mem;
    char* name = nullptr; ///< Objektname für Diagnosemeldungen

    // Setzt den Objektnamen für Diagnosemeldungen
    void set_name(const char* new_name) {
        if (name) {
            free(name);
            name = nullptr;
        }
        if (new_name) {
            std::size_t len = std::strlen(new_name) + 1;
            if (psram_found()) {
                name = static_cast<char*>(psram_malloc(len));
            } else {
                name = static_cast<char*>(malloc(len));
            }
            if (name) {
                std::memcpy(name, new_name, len);
            } else {
                printf("OOM: failed to allocate %zu bytes for name %s\n", len, new_name);
            }
        }
    }

  public:
    ps_struct_ptr() = default;

    // Konstruktor mit Objektnamen
    explicit ps_struct_ptr(const char* name_str) {
        set_name(name_str);
    }

    ~ps_struct_ptr() {
        if (mem) {
            ESP_LOGW(PS_PTR_TAG, "Destructor called for %s: Freeing %zu bytes at %p", name ? name : "unnamed", sizeof(T), mem.get());
        } else {
            ESP_LOGW(PS_PTR_TAG, "Destructor called for %s: No memory to free.", name ? name : "unnamed");
        }
        if (name) {
            free(name);
            name = nullptr;
        }
    }

    ps_struct_ptr(ps_struct_ptr&& other) noexcept {
        mem = std::move(other.mem);
        name = other.name;
        other.name = nullptr;
    }

    ps_struct_ptr& operator=(ps_struct_ptr&& other) noexcept {
        if (this != &other) {
            if (name) {
                free(name);
                name = nullptr;
            }
            mem = std::move(other.mem);
            name = other.name;
            other.name = nullptr;
        }
        return *this;
    }

    ps_struct_ptr(const ps_struct_ptr&) = delete;
    ps_struct_ptr& operator=(const ps_struct_ptr&) = delete;

    // Allokiert das Struct, bevorzugt im PSRAM, und ruft seinen Konstruktor auf
    void alloc(const char* name_str = nullptr) {
        reset();
        if (name_str) {
            set_name(name_str);
        }
        void* raw_mem = nullptr;
        if (psram_found()) {
            raw_mem = psram_malloc(sizeof(T));
        } else {
            raw_mem = malloc(sizeof(T));
        }
        if (raw_mem) {
            mem.reset(new (raw_mem) T());
        } else {
            printf("OOM: failed to allocate %zu bytes for %s\n", sizeof(T), name ? name : (name_str ? name_str : "unnamed"));
        }
    }

    // Wie alloc(), setzt den Speicher aber auf null statt den Konstruktor zu rufen
    void calloc(const char* name_str = nullptr) {
        reset();
        if (name_str) {
            set_name(name_str);
        }
        void* raw_mem = nullptr;
        if (psram_found()) {
            raw_mem = psram_calloc(1, sizeof(T));
        } else {
            raw_mem = calloc(1, sizeof(T));
        }
        if (raw_mem) {
            mem.reset(static_cast<T*>(raw_mem));
        } else {
            printf("OOM: failed to allocate %zu bytes for %s\n", sizeof(T), name ? name : (name_str ? name_str : "unnamed"));
        }
    }

    // Gibt das Struct frei, der Name bleibt bis zum Destruktor erhalten
    void reset() {
        mem.reset();
    }

    T* operator->() {
        return mem.get();
    }

    const T* operator->() const {
        return mem.get();
    }

    T& operator*() {
        return *mem;
    }

    const T& operator*() const {
        return *mem;
    }

    T* get() noexcept {
        return mem.get();
    }

    const T* get() const noexcept {
        return mem.get();
    }

    inline void free_all_ptr_members(); // Prototyp, Definition liefert das Makro PS_STRUCT_FREE_MEMBERS
    inline void free_field(char*& field);

    // Ersetzt ein char*-Feld des Structs durch eine Kopie des Texts, bevorzugt im PSRAM
    void set_ptr_field(char** field, const char* text) {
        if (!mem || !field)
            return;
        if (*field) {
            free(*field);
            *field = nullptr;
        }
        if (text) {
            std::size_t len = strlen(text) + 1;
            char* p = nullptr;
            if (psram_found()) {
                p = static_cast<char*>(psram_malloc(len));
            } else {
                p = static_cast<char*>(malloc(len));
            }
            if (p) {
                memcpy(p, text, len);
                *field = p;
            }
        }
    }

    bool valid() const {
        return mem.get() != nullptr;
    }
    std::size_t size() const {
        return sizeof(T);
    }
    void clear() {
        reset();
    }
};

    // ===================================================================
    // Makro: definiert für ein Struct die Freigabe seiner Zeiger-Felder
    #define PS_STRUCT_FREE_MEMBERS(TYPE, ...)                                                                                                                                      \
        template <> inline void ps_struct_ptr<TYPE>::free_all_ptr_members() {                                                                                                      \
            auto ptr = this->get(); /* 'ptr' ist hier ein Pointer auf die Struktur TYPE */                                                                                         \
            if (ptr) {              /* Sicherheitsprüfung, um sicherzustellen, dass ptr gültig ist */                                                                              \
                __VA_ARGS__                                                                                                                                                        \
            }                                                                                                                                                                      \
        }

inline void free_field(char*& field) {
    if (field) {
        free(field);
        field = nullptr;
    }
}

// Gibt mehrere char*-Felder in einem Aufruf frei
template <typename... Args> inline void free_fields(Args&... fields) {
    (free_field(fields), ...);
}

// ===================================================================
// ps_array2d
// ===================================================================

// Zweidimensionales Array im PSRAM mit geprüften Zugriffen. Verwendung:
//   ps_array2d<int32_t> s_samples;
//   s_samples.alloc(2, 1152);                       // Platz für [2][1152]
//   int32_t* pcm1 = s_samples.get_raw_row_ptr(0);   // Zeiger auf Zeile 0
//   pcm1[0] = 42;
//   s_samples.reset();
template <typename T> class ps_array2d {
  private:
    std::unique_ptr<T[], PsramDeleter> mem;
    size_t rows = 0;
    size_t cols = 0;
    char* name = nullptr; ///< Objektname für Diagnosemeldungen

    // Setzt den Objektnamen für Diagnosemeldungen
    void set_name(const char* new_name) {
        if (name) {
            free(name);
            name = nullptr;
        }
        if (new_name) {
            std::size_t len = std::strlen(new_name) + 1;
            if (psram_found()) {
                name = static_cast<char*>(psram_malloc(len));
            } else {
                name = static_cast<char*>(malloc(len));
            }
            if (name) {
                std::memcpy(name, new_name, len);
            } else {
                printf("OOM: failed to allocate %zu bytes for name %s\n", len, new_name);
            }
        }
    }

  public:
    // Standardkonstruktor
    ps_array2d() = default;

    // Konstruktor mit Namen
    explicit ps_array2d(const char* name_str) {
        set_name(name_str);
    }

    // Destruktor
    ~ps_array2d() {
        if (mem) {
            ESP_LOGW(PS_PTR_TAG, "Destructor called for %s: Freeing %zu bytes at %p", name ? name : "unnamed", rows * cols * sizeof(T), mem.get());
        } else {
            ESP_LOGW(PS_PTR_TAG, "Destructor called for %s: No memory to free.", name ? name : "unnamed");
        }
        if (name) {
            free(name);
            name = nullptr;
        }
    }

    // Move-Constructor
    ps_array2d(ps_array2d&& other) noexcept {
        mem = std::move(other.mem);
        rows = other.rows;
        cols = other.cols;
        name = other.name;
        other.rows = 0;
        other.cols = 0;
        other.name = nullptr;
    }

    // Move-Assignment-Operator
    ps_array2d& operator=(ps_array2d&& other) noexcept {
        if (this != &other) {
            if (name) {
                free(name);
                name = nullptr;
            }
            mem = std::move(other.mem);
            rows = other.rows;
            cols = other.cols;
            name = other.name;
            other.rows = 0;
            other.cols = 0;
            other.name = nullptr;
        }
        return *this;
    }

    ps_array2d(const ps_array2d&) = delete;
    ps_array2d& operator=(const ps_array2d&) = delete;

    // Allokiert Platz für r * c Elemente, bevorzugt im PSRAM, ohne Initialisierung
    void alloc(size_t r, size_t c, const char* alloc_name = nullptr, bool usePSRAM = true) {
        reset();
        if (alloc_name) {
            set_name(alloc_name);
        }
        rows = r;
        cols = c;
        size_t total_size = rows * cols * sizeof(T);
        total_size = (total_size + 15) & ~15; // Align to 16 bytes
        if (psram_found() && usePSRAM) {
            mem.reset(static_cast<T*>(psram_malloc(total_size)));
        } else {
            mem.reset(static_cast<T*>(malloc(total_size)));
        }
        if (!mem) {
            printf("OOM: failed to allocate %zu bytes for %s\n", total_size, name ? name : (alloc_name ? alloc_name : "unnamed"));
            rows = 0;
            cols = 0;
        }
    }

    // Wie alloc(), setzt den Speicher aber auf null
    void calloc(size_t r, size_t c, const char* alloc_name = nullptr, bool usePSRAM = true) {
        reset();
        if (alloc_name) {
            set_name(alloc_name);
        }
        rows = r;
        cols = c;
        size_t total_size = rows * cols * sizeof(T);
        total_size = (total_size + 15) & ~15; // Align to 16 bytes
        void* raw_mem = nullptr;
        if (psram_found() && usePSRAM) {
            raw_mem = psram_calloc(1, total_size);
        } else {
            raw_mem = calloc(1, total_size);
        }
        if (raw_mem) {
            mem.reset(static_cast<T*>(raw_mem));
        } else {
            printf("OOM: failed to allocate %zu bytes for %s\n", total_size, name ? name : (alloc_name ? alloc_name : "unnamed"));
            rows = 0;
            cols = 0;
        }
    }

    // Gibt den Speicher frei, der Name bleibt bis zum Destruktor erhalten
    void reset() {
        mem.reset();
        rows = 0;
        cols = 0;
    }

    // Zeilenzugriff, liefert den Zeiger auf den Zeilenanfang
    T* operator[](size_t row) {
        return mem.get() + row * cols;
    }

    const T* operator[](size_t row) const {
        return mem.get() + row * cols;
    }

    T* get() {
        return mem.get();
    }

    const T* get() const {
        return mem.get();
    }

    bool valid() const {
        return mem.get() != nullptr;
    }
    size_t get_rows() const {
        return rows;
    }
    size_t get_cols() const {
        return cols;
    }
};

// ===================================================================
// ps_array3d
// ===================================================================

// Dreidimensionales Array im PSRAM. Verwendung:
//   ps_array3d<int32_t> s_sbsample;
//   s_sbsample.alloc(2, 36, 32);
//   int32_t (*sample)[32] = reinterpret_cast<int32_t(*)[32]>(s_sbsample.get_raw_row_ptr(ch, 18 * gr));
//   (*sample)[0] = 10;
//   s_sbsample.reset();
template <typename T> class ps_array3d {
  private:
    std::unique_ptr<T[], PsramDeleter> mem;
    size_t dim1 = 0;
    size_t dim2 = 0;
    size_t dim3 = 0;
    char* name = nullptr; ///< Objektname für Diagnosemeldungen

    // Setzt den Objektnamen für Diagnosemeldungen
    void set_name(const char* new_name) {
        if (name) {
            free(name);
            name = nullptr;
        }
        if (new_name) {
            std::size_t len = std::strlen(new_name) + 1;
            if (psram_found()) {
                name = static_cast<char*>(psram_malloc(len));
            } else {
                name = static_cast<char*>(malloc(len));
            }
            if (name) {
                std::memcpy(name, new_name, len);
            } else {
                printf("OOM: failed to allocate %zu bytes for name %s\n", len, new_name);
            }
        }
    }

  public:
    // Standardkonstruktor
    ps_array3d() = default;

    // Konstruktor mit Namen
    explicit ps_array3d(const char* name_str) {
        set_name(name_str);
    }

    // Destruktor
    ~ps_array3d() {
        if (mem) {
            ESP_LOGW(PS_PTR_TAG, "Destructor called for %s: Freeing %zu bytes at %p", name ? name : "unnamed", dim1 * dim2 * dim3 * sizeof(T), mem.get());
        } else {
            ESP_LOGW(PS_PTR_TAG, "Destructor called for %s: No memory to free.", name ? name : "unnamed");
        }
        if (name) {
            free(name);
            name = nullptr;
        }
    }

    // Move-Konstruktor
    ps_array3d(ps_array3d&& other) noexcept {
        mem = std::move(other.mem);
        dim1 = other.dim1;
        dim2 = other.dim2;
        dim3 = other.dim3;
        name = other.name;
        other.dim1 = 0;
        other.dim2 = 0;
        other.dim3 = 0;
        other.name = nullptr;
    }

    // Move-Assignment-Operator
    ps_array3d& operator=(ps_array3d&& other) noexcept {
        if (this != &other) {
            if (name) {
                free(name);
                name = nullptr;
            }
            mem = std::move(other.mem);
            dim1 = other.dim1;
            dim2 = other.dim2;
            dim3 = other.dim3;
            name = other.name;
            other.dim1 = 0;
            other.dim2 = 0;
            other.dim3 = 0;
            other.name = nullptr;
        }
        return *this;
    }

    ps_array3d(const ps_array3d&) = delete;
    ps_array3d& operator=(const ps_array3d&) = delete;

    // Allokiert Platz für d1 * d2 * d3 Elemente, bevorzugt im PSRAM, ohne Initialisierung
    void alloc(size_t d1, size_t d2, size_t d3, const char* alloc_name = nullptr, bool usePSRAM = true) {
        reset();
        if (alloc_name) {
            set_name(alloc_name);
        }
        dim1 = d1;
        dim2 = d2;
        dim3 = d3;
        size_t total_size = dim1 * dim2 * dim3 * sizeof(T);
        total_size = (total_size + 15) & ~15; // Align to 16 bytes
        if (psram_found() && usePSRAM) {
            mem.reset(static_cast<T*>(psram_malloc(total_size)));
        } else {
            mem.reset(static_cast<T*>(malloc(total_size)));
        }
        if (!mem) {
            printf("OOM: failed to allocate %zu bytes for %s\n", total_size, name ? name : (alloc_name ? alloc_name : "unnamed"));
            dim1 = 0;
            dim2 = 0;
            dim3 = 0;
        }
    }

    // Wie alloc(), setzt den Speicher aber auf null
    void calloc(size_t d1, size_t d2, size_t d3, const char* alloc_name = nullptr, bool usePSRAM = true) {
        reset();
        if (alloc_name) {
            set_name(alloc_name);
        }
        dim1 = d1;
        dim2 = d2;
        dim3 = d3;
        size_t total_size = dim1 * dim2 * dim3 * sizeof(T);
        total_size = (total_size + 15) & ~15; // Align to 16 bytes
        void* raw_mem = nullptr;
        if (psram_found() && usePSRAM) {
            raw_mem = psram_calloc(1, total_size);
        } else {
            raw_mem = calloc(1, total_size);
        }
        if (raw_mem) {
            mem.reset(static_cast<T*>(raw_mem));
        } else {
            printf("OOM: failed to allocate %zu bytes for %s\n", total_size, name ? name : (alloc_name ? alloc_name : "unnamed"));
            dim1 = 0;
            dim2 = 0;
            dim3 = 0;
        }
    }

    // Gibt den Speicher frei, der Name bleibt bis zum Destruktor erhalten
    void reset() {
        mem.reset();
        dim1 = 0;
        dim2 = 0;
        dim3 = 0;
    }

    // Zugriff über [d1][d2] liefert den Zeiger auf die innerste Reihe
    class Proxy {
        T* ptr;
        size_t cols, depth;

      public:
        Proxy(T* p, size_t c, size_t d) : ptr(p), cols(c), depth(d) {}
        T* operator[](size_t col) {
            return ptr + col * depth;
        }
        const T* operator[](size_t col) const {
            return ptr + col * depth;
        }
    };

    Proxy operator[](size_t d1) {
        return Proxy(mem.get() + d1 * dim2 * dim3, dim2, dim3);
    }

    const Proxy operator[](size_t d1) const {
        return Proxy(mem.get() + d1 * dim2 * dim3, dim2, dim3);
    }

    T* get() {
        return mem.get();
    }

    const T* get() const {
        return mem.get();
    }

    bool valid() const {
        return mem.get() != nullptr;
    }
    size_t get_dim1() const {
        return dim1;
    }
    size_t get_dim2() const {
        return dim2;
    }
    size_t get_dim3() const {
        return dim3;
    }
};
#endif

#endif // AUDIO_LITE_PSRAM_UNIQUE_PTR_H
