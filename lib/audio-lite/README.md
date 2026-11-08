# audio-lite

Arduino-freier Ersatz für [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) von Wolle (schreibfaul1), reduziert auf den Funktionsumfang, den das RetroRadio-Projekt tatsächlich benutzt. Die Library spricht intern ausschließlich ESP-IDF-APIs (i2s_std, lwIP, FreeRTOS) und baut trotzdem als PlatformIO-Library im Arduino-Framework.

## Herkunft und Änderungen

Dieses Projekt ist eine veränderte Fassung von **ESP32-audioI2S, Version 3.4.7** (Autor: Wolle alias schreibfaul1), übernommen im August 2026 und seitdem umgebaut. Diese Kennzeichnung erfolgt gemäß GPLv3 §5a.

Die wesentlichen Änderungen gegenüber dem Original:

- Alle Arduino-Abhängigkeiten entfernt (Arduino.h, String, WiFiClient, FS/SD). Dateizugriff läuft über POSIX (`FILE*`, `connecttoFS(const char* pfad)`), Netzwerk über eine eigene Socket-Schicht.
- Neue Netzwerkschicht: abstrakte Schnittstelle `NetSocket` mit der Implementierung `TcpSocket` direkt über lwIP. Die Lesesemantik entspricht bewusst Arduinos NetworkClient (nicht blockierende reads). Die zweite Implementierung `TlsSocket` spricht https über esp-tls und prüft das Serverzertifikat gegen das im Framework eingebaute Zertifikatsbündel.
- Eigener Ringpuffer (`AudioBuffer`) als Ersatz für die upstream-interne Pufferklasse.
- Opus-Decoder entfernt, VU/Spektrum-Berechnung stillgelegt (das Host-Projekt verarbeitet Pegel und Spektrum selbst über den Visualizer-Callback `audio_visualizer_process_i2s`).
- Fehlerkorrekturen, die im Original latent vorhanden sind: ID3-Parser-Kette (Fenstergrenzen im MP3_TAG-Zustand, Allokationsreihenfolge in `ps_ptr`, memcpy-Wächter) sowie ein terminaler Header-Timeout beim Verbindungsaufbau.
- Die Decoder-Kerne (mp3, aac/libfaad, flac, vorbis, wav) sind bis auf mechanische Anpassungen (Include-Guards, Ersatz der Arduino-min/max-Makros, Speicher-Helfer) und die folgenden Korrekturen unverändert übernommen.
- Im Tab5-Projekt seit 03.11.2026 aus RetroRadio übernommen ([docs/PORTIERUNG.md](../../docs/PORTIERUNG.md)): AAC-Stereo-Umsetzung über die gültigen Werte, spät signalisiertes parametrisches Stereo ohne Decoderneustart, Pegelreserve 181/256 (rund 3 dB) für AAC, Vorbis, WAV und FLAC wie beim MP3-Decoder, OGG-Endabfrage und Vorbis-Lesezähler für lokale und Web-Dateien, OGG-Webstart, WAV-Nachschub und ein Merker, der einen Ausgabeblock nach einem Schreibversuch ohne übernommene Frames nicht erneut verarbeitet. Eigene Änderung im Tab5-Projekt: FLAC gibt jeden Rahmen in Teilen zu höchstens 1.024 Frames aus ([docs/SDPLAYER.md](../../docs/SDPLAYER.md)).

## Lizenz

GPL-3.0, wie das Original. Der vollständige Lizenztext liegt in [LICENSE](LICENSE). Die Urheberrechte am ursprünglichen Code liegen bei Wolle (schreibfaul1) und den jeweiligen Autoren der Decoder-Kerne. Die Autorenvermerke in den Dateiköpfen bleiben erhalten.

In der Library enthaltene Fremdanteile (unverändert aus ESP32-audioI2S übernommen):

| Bestandteil | Herkunft | Lizenz |
|---|---|---|
| MP3-Decoder | basiert auf dem Helix-MP3-Decoder (RealNetworks), in der von ESP32-audioI2S verbreiteten Fassung | wie im Upstream verbreitet |
| AAC-Decoder | libfaad2 (M. Bakker, Nero AG) | GPL-2.0-or-later |
| Vorbis-Decoder | Xiph.Org Foundation | BSD-artig (Xiph) |
| FLAC/WAV-Decoder | ESP32-audioI2S | GPL-3.0 |

## Nutzung

Die Library ersetzt `lib/ESP32-audioI2S` als Drop-in in einem PlatformIO-Arduino-Projekt. Die öffentliche API entspricht dem Original. Einziger Unterschied ist `connecttoFS(const char* pfad)` mit POSIX-Pfad (Arduinos `SD.begin()` hängt die Karte unter `/sd` ein, Pfade brauchen also dieses Präfix).
