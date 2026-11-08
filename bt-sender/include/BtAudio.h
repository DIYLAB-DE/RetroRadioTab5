#ifndef BT_AUDIO_H
#define BT_AUDIO_H

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

/** @brief Tonwege des Pico ohne Hardwarezugriff, per Hosttest geprüft: Umrechnung der Quellrate des Tab5 auf die Ausgaberate samt Ausgleich der Taktdrift, Ringpuffer zwischen I2S-Task und Ausgabe, Ausgabe mit Ein- und Ausblenden und die S/PDIF-Kodierung für den optischen Ausgang. Anschlüsse und Protokoll in bt-sender/README.md. */
namespace BtAudio
{
    constexpr uint32_t OutputRate = 44100;                                  ///< Die A2DP-Quelle von Bluedroid verlangt 44,1 kHz, 16 Bit Stereo
    constexpr uint32_t MinInputRate = 8000;                                 ///< Kleinste Quellrate, wie audio-lite
    constexpr uint32_t MaxInputRate = 96000;                                ///< Größte Quellrate, der I2S-Empfang ist dafür eingestellt
    constexpr size_t Taps = 64;                                             ///< Eingangswerte je Ausgabewert des Bluetooth-Wegs
    constexpr size_t OpticalTaps = 32;                                      ///< Eingangswerte je Ausgabewert des optischen Wegs: halbe Rechenzeit und Tabelle, Durchlass bis rund 17 kHz bei 44,1 kHz statt 18 kHz (Hosttest)
    constexpr size_t PhaseBits = 6;                                         ///< Tabellenzeilen je Eingangsabstand als Zweierpotenz, dazwischen linear
    constexpr size_t Phases = size_t{1} << PhaseBits;                       ///< 64 Zeilen. Der Fehler der linearen Zwischenwerte liegt unter der Sperrdämpfung (Hosttest: Abstand zum idealen Sinus mindestens 78 dB, mit 128 Zeilen nicht besser)
    constexpr float StopbandDb = 60.0f;                                     ///< Sperrdämpfung des Tiefpasses, Spiegel- und Faltungsanteile liegen mindestens so tief
    constexpr float KaiserBeta = 0.1102f * (StopbandDb - 8.7f);             ///< Form des Kaiser-Fensters für StopbandDb
    constexpr uint32_t RingFrames = 4096;                                   ///< Ringpuffer zwischen I2S-Task und Datencallback, rund 93 ms. Zweierpotenz. Der Callback holt in Schüben, gemessen schwankt der Füllstand um bis zu rund 1.000 Frames um das Soll
    constexpr uint32_t TargetFrames = 2048;                                 ///< Sollfüllstand des Bluetooth-Rings, rund 46 ms. Zugleich Vorlauf nach Start und Neustart
    constexpr uint32_t OpticalRingFrames = 2048;                            ///< Ringpuffer zum optischen Ausgang, die I2S-Ausgabe holt gleichmäßig in Blöcken
    constexpr uint32_t OpticalTargetFrames = 768;                           ///< Sollfüllstand des optischen Rings, rund 17 ms bei 44,1 kHz
    constexpr uint32_t OpticalMinRate = 32000;                              ///< Kleinste Rate am optischen Ausgang. Kleinere Quellraten werden verdoppelt, weil viele Empfänger erst ab 32 kHz einrasten
    constexpr uint32_t OpticalMaxRate = 48000;                              ///< Größte Rate am optischen Ausgang. Größere Quellraten werden halbiert, das begrenzt die Rechenlast
    constexpr uint32_t FadeFrames = 441;                                    ///< 10 ms Ein- und Ausblenden bei Stumm, Start und Neustart
    constexpr uint32_t TailFrames = 64;                                     ///< Ausklingen des letzten Werts bei einem Unterlauf, 1,5 ms
    constexpr float ControlGainPpm = 2000.0f;                               ///< Korrektur bei einer Abweichung vom Sollfüllstand um 100 % (Proportionalteil)
    constexpr float ControlIntegralSeconds = 60.0f;                         ///< Nachstellzeit des Integralteils, der den bleibenden Taktunterschied lernt
    constexpr float ControlSmoothingSeconds = 2.0f;                         ///< Glättung des Füllstands, der Datencallback holt in Schüben
    constexpr float MaxCorrectionPpm = 5000.0f;                             ///< Größte Korrektur, rund 9 Cent Tonhöhe
    static_assert(TargetFrames + 2 * FadeFrames < RingFrames, "Sollfüllstand und Ausblenden müssen in den Ring passen");
    static_assert(OpticalTargetFrames + 2 * FadeFrames < OpticalRingFrames, "Sollfüllstand und Ausblenden müssen in den optischen Ring passen");

    /** @brief Rate am optischen Ausgang zu einer Quellrate: die Quellrate selbst, verdoppelt bis mindestens OpticalMinRate, halbiert bis höchstens OpticalMaxRate. */
    constexpr uint32_t opticalRate(uint32_t source)
    {
        uint32_t rate = source;
        while (rate > OpticalMaxRate)
            rate /= 2;
        while (rate < OpticalMinRate)
            rate *= 2;
        return rate;
    }

    /** @brief Ein Abtastpaar wie die A2DP-Quelle es erwartet. */
    struct StereoFrame
    {
        int16_t left = 0;  ///< Linker Kanal
        int16_t right = 0; ///< Rechter Kanal
    };

    /** @brief Rundet auf int16 mit Sättigung. */
    inline int16_t toSample(float value)
    {
        const float rounded = value >= 0.0f ? value + 0.5f : value - 0.5f;
        return static_cast<int16_t>(std::min(32767.0f, std::max(-32768.0f, rounded)));
    }

    /** @brief Modifizierte Besselfunktion erster Art nullter Ordnung für das Kaiser-Fenster, als Reihe bis zur Genauigkeit von float. */
    inline float besselI0(float x)
    {
        const float half = 0.5f * x;
        float term = 1.0f, sum = 1.0f;
        for (int k = 1; k < 50 && term > 1e-9f * sum; ++k)
        {
            const float factor = half / static_cast<float>(k);
            term *= factor * factor;
            sum += term;
        }
        return sum;
    }

    /** @brief Ringpuffer mit Frames Plätzen für einen Schreiber (I2S-Task) und einen Leser (Ausgabe) ohne Sperre. Lese- und Schreibzähler laufen frei über und werden maskiert. */
    template <uint32_t Frames>
    class FrameRing
    {
    public:
        static_assert((Frames & (Frames - 1)) == 0, "Frames muss eine Zweierpotenz sein");
        static constexpr uint32_t Capacity = Frames; ///< Plätze

        /** @brief Belegte Frames aus Sicht beider Seiten, der Schreiber zählt nur bis zum letzten commit(). */
        uint32_t fill() const
        {
            return m_write.load(std::memory_order_acquire) - m_read.load(std::memory_order_acquire);
        }

        /** @brief Schreiber: legt ein Frame ab, sichtbar erst nach commit(). @return false bei vollem Puffer. Das Frame entfällt dann. */
        bool push(StereoFrame frame)
        {
            if (m_pending - m_read.load(std::memory_order_acquire) >= Frames)
                return false;
            m_frames[m_pending++ & (Frames - 1)] = frame;
            return true;
        }

        /** @brief Schreiber: macht alle seit dem letzten Aufruf abgelegten Frames für den Leser sichtbar. */
        void commit()
        {
            m_write.store(m_pending, std::memory_order_release);
        }

        /** @brief Leser: nimmt das älteste Frame. @return false bei leerem Puffer. */
        bool pop(StereoFrame &frame)
        {
            const uint32_t read = m_read.load(std::memory_order_relaxed);
            if (read == m_write.load(std::memory_order_acquire))
                return false;
            frame = m_frames[read & (Frames - 1)];
            m_read.store(read + 1, std::memory_order_release);
            return true;
        }

        /** @brief Leser: verwirft die ältesten Frames, höchstens alle belegten. */
        void discard(uint32_t frames)
        {
            const uint32_t read = m_read.load(std::memory_order_relaxed);
            m_read.store(read + std::min(frames, m_write.load(std::memory_order_acquire) - read), std::memory_order_release);
        }

    private:
        StereoFrame m_frames[Frames]{};   ///< Speicher, Index maskiert
        uint32_t m_pending = 0;           ///< Schreibzähler einschließlich noch nicht sichtbarer Frames, nur Schreiber
        std::atomic<uint32_t> m_write{0}; ///< Sichtbarer Schreibzähler
        std::atomic<uint32_t> m_read{0};  ///< Lesezähler
    };

    /**
     * @brief Rechnet Stereo-Abtastwerte einer Rate in MinInputRate..MaxInputRate auf eine Ausgaberate um: Kaiser-gefensterter Sinc-Tiefpass mit TapCount Stützstellen in einer Tabelle mit Phases Zeilen, Zwischenwerte linear.
     *
     * Der Tiefpass sperrt ab der halben kleineren der beiden Raten mit StopbandDb und lässt darunter bis zur Übergangsbreite alles unverändert durch. Bei gleicher Rate ist das ein reiner Zeitversatz um eine halbe Tabellenbreite. Die Schrittweite trägt zusätzlich die Korrektur aus DriftControl, sodass Umrechnung und Driftausgleich ein Rechenschritt sind. Ohne Allokation. Nur in einem Task.
     */
    template <size_t TapCount = Taps>
    class RateConverter
    {
    public:
        static_assert((TapCount & (TapCount - 1)) == 0, "TapCount muss eine Zweierpotenz sein");

        /** @brief Rechnet die Filtertabelle für inputRate -> outputRate, setzt Verlauf und Korrektur zurück. Dauert auf dem Pico einige Millisekunden. @return false außerhalb MinInputRate..MaxInputRate. Die bisherige Tabelle bleibt dann. */
        bool configure(uint32_t inputRate, uint32_t outputRate = OutputRate)
        {
            if (inputRate < MinInputRate || inputRate > MaxInputRate || outputRate < MinInputRate || outputRate > MaxInputRate)
                return false;
            constexpr float Pi = 3.14159265358979f;
            const float rate = static_cast<float>(inputRate);
            const float stop = 0.5f * static_cast<float>(std::min(inputRate, outputRate));
            // Übergangsbreite eines Kaiser-Fensters mit TapCount Stützstellen für StopbandDb, in Hz der Eingangsrate.
            const float transition = (StopbandDb - 7.95f) / (14.36f * static_cast<float>(TapCount - 1)) * rate;
            const float cutoff = (stop - 0.5f * transition) / rate;
            const float half = 0.5f * static_cast<float>(TapCount);
            const float window = besselI0(KaiserBeta);
            for (size_t p = 0; p <= Phases; ++p)
            {
                const float fraction = static_cast<float>(p) / static_cast<float>(Phases);
                float sum = 0.0f;
                for (size_t j = 0; j < TapCount; ++j)
                {
                    const float t = half - 1.0f + fraction - static_cast<float>(j);
                    const float x = t / half;
                    const float shape = besselI0(KaiserBeta * std::sqrt(std::max(0.0f, 1.0f - x * x))) / window;
                    const float arg = 2.0f * cutoff * t;
                    const float sinc = arg == 0.0f ? 1.0f : std::sin(Pi * arg) / (Pi * arg);
                    m_table[p][j] = 2.0f * cutoff * sinc * shape;
                    sum += m_table[p][j];
                }
                // Jede Zeile auf Gleichanteil 1, damit die Verstärkung nicht mit der Phase schwankt.
                for (size_t j = 0; j < TapCount; ++j)
                    m_table[p][j] /= sum;
            }
            m_rate = inputRate;
            m_outputRate = outputRate;
            m_baseStep = (static_cast<uint64_t>(inputRate) << 32) / outputRate;
            m_step = m_baseStep;
            reset();
            return true;
        }

        /** @brief Quellrate der aktuellen Tabelle in Hz, 0 vor dem ersten configure(). */
        uint32_t inputRate() const
        {
            return m_rate;
        }

        /** @brief Ausgaberate der aktuellen Tabelle in Hz, 0 vor dem ersten configure(). */
        uint32_t outputRate() const
        {
            return m_outputRate;
        }

        /** @brief Löscht den Verlauf, die nächsten Ausgabewerte klingen aus Stille ein. */
        void reset()
        {
            std::fill(m_left, m_left + 2 * TapCount, 0.0f);
            std::fill(m_right, m_right + 2 * TapCount, 0.0f);
            m_newest = 0;
            m_time = 0;
        }

        /** @brief Setzt die Korrektur der Schrittweite. @param ppm Positiv liefert weniger Ausgabewerte je Eingangswert und leert damit den Ring. */
        void setCorrection(float ppm)
        {
            m_step = static_cast<uint64_t>(static_cast<double>(m_baseStep) * (1.0 + 1e-6 * static_cast<double>(ppm)));
        }

        /**
         * @brief Nimmt count Stereo-Frames im Format des I2S-Empfangs und gibt die fälligen Ausgabewerte an sink.
         * @param frames Je Frame links und rechts als 32-Bit-Wert mit dem Signal in den oberen 16 Bit (Q31)
         * @param sink Aufruf sink(int16_t links, int16_t rechts) je Ausgabewert
         */
        template <typename Sink>
        void process(const int32_t *frames, size_t count, Sink &&sink)
        {
            constexpr uint64_t One = uint64_t{1} << 32;
            constexpr uint32_t FractionBits = 32 - PhaseBits;
            constexpr float InputScale = 1.0f / 65536.0f;
            constexpr float FractionScale = 1.0f / static_cast<float>(uint32_t{1} << FractionBits);
            for (size_t i = 0; i < count; ++i)
            {
                const float left = static_cast<float>(frames[2 * i]) * InputScale, right = static_cast<float>(frames[2 * i + 1]) * InputScale;
                m_left[m_newest] = m_left[m_newest + TapCount] = left;
                m_right[m_newest] = m_right[m_newest + TapCount] = right;
                // Doppelt abgelegt: Die TapCount jüngsten Werte stehen ab start lückenlos, ältester zuerst.
                const size_t start = m_newest + 1;
                m_newest = (m_newest + 1) & (TapCount - 1);
                while (m_time < One)
                {
                    const uint32_t fraction = static_cast<uint32_t>(m_time);
                    const size_t phase = fraction >> FractionBits;
                    const float weight = static_cast<float>(fraction & ((uint32_t{1} << FractionBits) - 1)) * FractionScale;
                    const float *row = m_table[phase], *next = m_table[phase + 1];
                    const float *xl = m_left + start, *xr = m_right + start;
                    float sumLeft = 0.0f, sumRight = 0.0f;
                    for (size_t j = 0; j < TapCount; ++j)
                    {
                        const float c = row[j] + weight * (next[j] - row[j]);
                        sumLeft += c * xl[j];
                        sumRight += c * xr[j];
                    }
                    sink(toSample(sumLeft), toSample(sumRight));
                    m_time += m_step;
                }
                m_time -= One;
            }
        }

    private:
        float m_table[Phases + 1][TapCount]{}; ///< Gewichte je Phase, ältester Eingangswert zuerst. Zeile Phases gehört zum nächsten Eingangsabstand
        float m_left[2 * TapCount]{};          ///< Verlauf links, jeder Wert doppelt
        float m_right[2 * TapCount]{};         ///< Verlauf rechts, jeder Wert doppelt
        size_t m_newest = 0;                   ///< Nächster Schreibplatz im Verlauf
        uint64_t m_time = 0;                   ///< Zeit des nächsten Ausgabewerts nach dem jüngsten verarbeiteten Eingangswert in Eingangsabständen, Q32
        uint64_t m_baseStep = 0;               ///< Eingangsabstände je Ausgabewert ohne Korrektur, Q32
        uint64_t m_step = 0;                   ///< Mit Korrektur, Q32
        uint32_t m_rate = 0;                   ///< Quellrate der Tabelle in Hz
        uint32_t m_outputRate = 0;             ///< Ausgaberate der Tabelle in Hz
    };

    /** @brief PI-Regler, der aus dem Füllstand eines Rings die Korrektur der Schrittweite bildet. Der Integralteil lernt den bleibenden Taktunterschied zwischen Tab5 und Ausgabe und bleibt über Neustarte der Ausgabe erhalten. */
    class DriftControl
    {
    public:
        /** @param target Sollfüllstand in Frames. */
        explicit DriftControl(uint32_t target = TargetFrames) : m_target(static_cast<float>(target)), m_smoothed(m_target)
        {
        }

        /** @brief Vergisst auch den gelernten Taktunterschied, etwa bei einer neuen Quellrate. */
        void reset()
        {
            m_smoothed = m_target;
            m_integral = 0.0f;
        }

        /** @brief Ohne laufende Ausgabe: verwirft die Füllstandsmessung und liefert nur den gelernten Taktunterschied. @return Korrektur in ppm. */
        float idle()
        {
            m_smoothed = m_target;
            return m_integral;
        }

        /** @brief Nimmt einen Füllstand auf. @param fill Belegte Frames im Ring. @param seconds Seit dem letzten Aufruf verarbeitete Eingangszeit. @return Korrektur in ppm, begrenzt auf MaxCorrectionPpm. */
        float update(uint32_t fill, float seconds)
        {
            m_smoothed += seconds / (seconds + ControlSmoothingSeconds) * (static_cast<float>(fill) - m_smoothed);
            const float error = (m_smoothed - m_target) / m_target;
            m_integral = std::min(MaxCorrectionPpm, std::max(-MaxCorrectionPpm, m_integral + ControlGainPpm * error * seconds / ControlIntegralSeconds));
            return std::min(MaxCorrectionPpm, std::max(-MaxCorrectionPpm, ControlGainPpm * error + m_integral));
        }

        /** @brief Gelernter Taktunterschied in ppm. */
        float learned() const
        {
            return m_integral;
        }

    private:
        float m_target;          ///< Sollfüllstand in Frames
        float m_smoothed;        ///< Geglätteter Füllstand in Frames
        float m_integral = 0.0f; ///< Integralteil in ppm
    };

    /**
     * @brief Ausgabe aus einem Ring: blendet bei Stumm, Start und Neustart über FadeFrames ein und aus und lässt bei einem Unterlauf den letzten Wert ausklingen, statt zu knacken.
     *
     * Nach dem Start, einem Unterlauf und einem Wechsel der Quellrate wartet sie stumm, bis der Ring den Sollfüllstand hält. Bei einem Wechsel der Quellrate blendet sie aus und verwirft den ganzen Ring, bei einem Überlauf blendet sie aus und verwirft nur den Überschuss über das Soll. Nur im Task der jeweiligen Ausgabe: A2DP-Datencallback oder optischer Task.
     */
    class OutputStage
    {
    public:
        /** @brief Zustand der Ausgabe. */
        enum class State : uint8_t
        {
            Waiting, ///< Stille, bis der Ring das Soll hält
            Playing, ///< Liest den Ring
            Fading   ///< Blendet vor dem Verwerfen aus
        };

        /** @param target Sollfüllstand und Vorlauf in Frames. */
        explicit OutputStage(uint32_t target = TargetFrames) : m_target(target)
        {
        }

        /**
         * @brief Liefert count Frames an sink(Index, links, rechts).
         * @param muted Wunsch des Tab5, wirkt über FadeFrames
         * @param generation Zähler der Quellrate, jede Änderung verwirft den Ring
         * @param overflow Der Schreiber fand den Ring voll
         */
        template <typename Ring, typename Sink>
        void render(Ring &ring, size_t count, bool muted, uint32_t generation, bool overflow, Sink &&sink)
        {
            constexpr float Step = 1.0f / static_cast<float>(FadeFrames);
            if (generation != m_generation)
            {
                m_generation = generation;
                restart(ring, true);
            }
            else if (overflow)
                restart(ring, false);
            for (size_t i = 0; i < count; ++i)
            {
                if (m_state == State::Waiting)
                {
                    if (m_tail > 0 || ring.fill() < m_target)
                    {
                        const float left = tail(m_tailLeft), right = tail(m_tailRight);
                        if (m_tail > 0)
                            --m_tail;
                        sink(i, toSample(left), toSample(right));
                        continue;
                    }
                    m_state = State::Playing;
                    m_gain = 0.0f;
                }
                StereoFrame frame;
                if (!ring.pop(frame))
                {
                    m_underruns.fetch_add(1, std::memory_order_relaxed);
                    m_tailLeft = m_lastLeft;
                    m_tailRight = m_lastRight;
                    m_tail = TailFrames;
                    m_lastLeft = m_lastRight = 0.0f;
                    m_state = State::Waiting;
                    sink(i, toSample(tail(m_tailLeft)), toSample(tail(m_tailRight)));
                    --m_tail;
                    continue;
                }
                m_gain = m_state == State::Fading ? std::max(0.0f, m_gain - Step) : std::min(1.0f, m_gain + Step);
                m_muteGain = muted ? std::max(0.0f, m_muteGain - Step) : std::min(1.0f, m_muteGain + Step);
                const float gain = m_gain * m_muteGain;
                m_lastLeft = static_cast<float>(frame.left) * gain;
                m_lastRight = static_cast<float>(frame.right) * gain;
                sink(i, toSample(m_lastLeft), toSample(m_lastRight));
                if (m_state == State::Fading && m_gain <= 0.0f)
                    flush(ring);
            }
            m_playing.store(m_state != State::Waiting, std::memory_order_relaxed);
        }

        /** @brief Die Ausgabe liest den Ring, nur dann misst DriftControl den Füllstand. Aus jedem Task. */
        bool playing() const
        {
            return m_playing.load(std::memory_order_relaxed);
        }

        /** @brief Unterläufe seit dem Start. Aus jedem Task. */
        uint32_t underruns() const
        {
            return m_underruns.load(std::memory_order_relaxed);
        }

        /** @brief Neustarts wegen Überlauf oder neuer Quellrate seit dem Start. Aus jedem Task. */
        uint32_t restarts() const
        {
            return m_restarts.load(std::memory_order_relaxed);
        }

    private:
        /** @brief Wert des Ausklingens nach einem Unterlauf: linear von start auf 0 über TailFrames. */
        float tail(float start) const
        {
            return start * static_cast<float>(m_tail) / static_cast<float>(TailFrames);
        }

        /** @brief Beginnt einen Neustart: still wird sofort verworfen, sonst erst nach dem Ausblenden. @param all Den ganzen Ring verwerfen statt nur den Überschuss über das Soll. */
        template <typename Ring>
        void restart(Ring &ring, bool all)
        {
            m_restarts.fetch_add(1, std::memory_order_relaxed);
            m_flushAll = all;
            if (m_state == State::Playing)
                m_state = State::Fading;
            else if (m_state == State::Waiting)
                flush(ring);
        }

        /** @brief Verwirft nach dem Ausblenden. Ganz verworfen wartet die Ausgabe auf den Vorlauf, sonst blendet sie sofort wieder ein. */
        template <typename Ring>
        void flush(Ring &ring)
        {
            const uint32_t fill = ring.fill();
            m_lastLeft = m_lastRight = 0.0f;
            if (m_flushAll)
            {
                ring.discard(fill);
                m_state = State::Waiting;
                return;
            }
            if (fill > m_target)
                ring.discard(fill - m_target);
            if (m_state == State::Fading)
                m_state = State::Playing;
        }

        uint32_t m_target;                    ///< Sollfüllstand und Vorlauf in Frames
        State m_state = State::Waiting;       ///< Aktueller Zustand
        float m_gain = 0.0f;                  ///< Blende für Start und Neustart, 0 bis 1
        float m_muteGain = 0.0f;              ///< Blende für Stumm, 0 bis 1. Beginnt stumm
        float m_lastLeft = 0.0f;              ///< Zuletzt ausgegebener Wert links, Ausgangspunkt des Ausklingens
        float m_lastRight = 0.0f;             ///< Ebenso rechts
        float m_tailLeft = 0.0f;              ///< Ausgangswert des laufenden Ausklingens links
        float m_tailRight = 0.0f;             ///< Ebenso rechts
        uint32_t m_tail = 0;                  ///< Verbleibende Frames des Ausklingens
        uint32_t m_generation = 0;            ///< Zuletzt gesehener Zähler der Quellrate
        bool m_flushAll = false;              ///< Der laufende Neustart verwirft den ganzen Ring
        std::atomic<bool> m_playing{false};   ///< Wie m_state != Waiting, für den I2S-Task
        std::atomic<uint32_t> m_underruns{0}; ///< Unterläufe
        std::atomic<uint32_t> m_restarts{0};  ///< Neustarts
    };

    /**
     * @brief S/PDIF-Kodierung nach IEC 60958 (Verbraucherformat) für den optischen Ausgang.
     *
     * Je Stereorahmen entstehen zwei Unterrahmen zu 64 Zellen als vier 32-Bit-Wörter, das höchste Bit zuerst, so wie die I2S-Einheit sie bei 32 Bit Stereo und doppelter Abtastrate lückenlos ausgibt. Die 28 Datenbits je Unterrahmen (Zeitschlitze 4 bis 31) sind Biphase-Mark-kodiert: ein Übergang am Anfang jedes Bits, ein zweiter in der Mitte einer Eins. Die Präambeln B, M und W verletzen diese Regel und markieren Blockanfang, linken und rechten Kanal. Sie haben vier Übergänge und enden auf dem Pegel vor ihnen. Der Abtastwert steht als 24-Bit-Wort mit acht Nullbits darunter, V (gültig) und U sind 0, C trägt den Kanalstatus mit der Abtastrate, P macht die Zahl der Einsen gerade. Ohne Allokation. Nur im optischen Task.
     */
    class SpdifEncoder
    {
    public:
        static constexpr uint32_t WordsPerFrame = 4; ///< Zwei Unterrahmen zu 64 Zellen
        static constexpr uint32_t BlockFrames = 192; ///< Rahmen je Block des Kanalstatus, der erste trägt links die Präambel B

        SpdifEncoder()
        {
            for (uint32_t value = 0; value < 16; ++value)
            {
                uint8_t cells = 0;
                bool level = false;
                for (int bit = 0; bit < 4; ++bit)
                {
                    level = !level;
                    cells = static_cast<uint8_t>(cells << 1 | (level ? 1 : 0));
                    if (value >> bit & 1)
                        level = !level;
                    cells = static_cast<uint8_t>(cells << 1 | (level ? 1 : 0));
                }
                m_nibble[value] = cells;
            }
            setRate(OutputRate);
        }

        /** @brief Trägt die Abtastrate in den Kanalstatus ein und beginnt einen neuen Block. Bits 24 bis 27, Bit 24 zuerst: 44,1 kHz 0000, 48 kHz 0100, 32 kHz 1100, sonst 0001 (nicht angegeben). */
        void setRate(uint32_t rate)
        {
            std::fill(m_status, m_status + sizeof(m_status), 0);
            // Byte 0: Verbraucherformat, PCM, Kopie erlaubt, keine Vorverzerrung, zwei Kanäle
            m_status[0] = 0x04;
            m_status[3] = rate == 44100 ? 0x0 : rate == 48000 ? 0x2
                                            : rate == 32000   ? 0x3
                                                              : 0x8;
            m_frame = 0;
            m_level = false;
        }

        /** @brief Kodiert einen Stereorahmen in WordsPerFrame Wörter ab words. */
        void encode(int16_t left, int16_t right, uint32_t *words)
        {
            const bool status = (m_status[m_frame >> 3] >> (m_frame & 7)) & 1;
            encodeSubframe(left, m_frame == 0 ? PreambleB : PreambleM, status, words);
            encodeSubframe(right, PreambleW, status, words + 2);
            if (++m_frame == BlockFrames)
                m_frame = 0;
        }

    private:
        static constexpr uint8_t PreambleB = 0xE8; ///< 11101000, erste Zelle zuerst: Blockanfang, linker Kanal
        static constexpr uint8_t PreambleM = 0xE2; ///< 11100010: linker Kanal
        static constexpr uint8_t PreambleW = 0xE4; ///< 11100100: rechter Kanal

        /** @brief Kodiert einen Unterrahmen in zwei Wörter, der Pegel läuft über Unterrahmen hinweg weiter. */
        void encodeSubframe(int16_t sample, uint8_t preamble, bool status, uint32_t *words)
        {
            // Zeitschlitze 4 bis 31 als Bits 0 bis 27: acht Nullbits, 16 Bit Abtastwert, V, U, C, P. Das niedrigste Bit wird zuerst gesendet.
            uint32_t data = static_cast<uint32_t>(static_cast<uint16_t>(sample)) << 8;
            data |= static_cast<uint32_t>(status ? 1 : 0) << 26;
            data |= static_cast<uint32_t>(__builtin_parity(data)) << 27;
            bool level = m_level;
            uint32_t word0 = static_cast<uint32_t>(preamble ^ (level ? 0xFF : 0x00)) << 24, word1 = 0;
            for (int nibble = 0; nibble < 7; ++nibble)
            {
                const uint8_t cells = static_cast<uint8_t>(m_nibble[(data >> (4 * nibble)) & 0xF] ^ (level ? 0xFF : 0x00));
                level = (cells & 1) != 0;
                if (nibble < 3)
                    word0 |= static_cast<uint32_t>(cells) << (16 - 8 * nibble);
                else
                    word1 |= static_cast<uint32_t>(cells) << (24 - 8 * (nibble - 3));
            }
            m_level = level;
            words[0] = word0;
            words[1] = word1;
        }

        uint8_t m_nibble[16]{};  ///< Acht Zellen je Datenhalbbyte bei Pegel 0 davor, erste Zelle im höchsten Bit. Bei Pegel 1 invertiert
        uint8_t m_status[24]{};  ///< Kanalstatus, Bit n im Rahmen n, niedrigstes Bit zuerst
        uint32_t m_frame = 0;    ///< Rahmen im Block, 0 bis BlockFrames - 1
        bool m_level = false;    ///< Pegel der letzten gesendeten Zelle
    };
}

#endif
