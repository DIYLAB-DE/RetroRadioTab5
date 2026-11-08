# RetroRadioTab5 Bluetooth-Sender (ESP32-PICO-D4)

[RetroRadioTab5](../README.md) > [Dokumentation](../README.md#dokumentation) > [Bluetooth-Sender](../docs/Bluetooth-Sender.md) > Pico-Firmware

Firmware für einen M5Stamp Pico (ESP32-PICO-D4), der aus einem I2S-Tonstrom ein Bluetooth-Audiogerät (A2DP: Kopfhörer, Lautsprecher, Autoradio, Soundbar), einen optischen S/PDIF-Ausgang und einen UKW-Modulator mit RDS bedient. Die Firmware kennt kein bestimmtes Gerät. Sie verbindet sich mit jeder Adresse, die der Host nennt, und speichert das zuletzt verbundene Gerät. Gesteuert wird sie über eine serielle Zeilenschnittstelle (UART). Der Pico ist nicht an ein bestimmtes Hostgerät gebunden: Jeder Host mit 3,3-V-UART und I2S-Master kann ihn benutzen.

Entstanden ist er für RetroRadioTab5 auf dem M5Stack Tab5. Bauteile, Verdrahtung, Flashen, Kopplung und Prüfstand für das Tab5 stehen in der Anleitung [Bluetooth-Sender](../docs/Bluetooth-Sender.md). Diese Seite beschreibt Anschlüsse und Protokoll des Pico für alle, die ihn an einem anderen Host betreiben oder die Firmware ändern möchten.

Firmwarestand: `5.3`. Lizenz: GPL-3.0 wie das Hauptprojekt, die Bibliothek ESP32-A2DP von Phil Schatzmann steht unter Apache-2.0 ([Lizenzen und Herkunft](../docs/LIZENZEN.md)).

## Anschlüsse

| Pico | Richtung | Funktion |
|---|---|---|
| 5V, GND (lange Reihe, auch Lötpunkte unten) | Eingang | Versorgung 5 V, eigener Wandler auf 3,3 V. Nie 5V und 3V3 gleichzeitig aus verschiedenen Quellen. Der Programmieradapter speist 3V3 |
| G25 | Eingang | UART-Empfang vom Host (Host TX). Pull-down: ohne Host liegt die Leitung Low |
| G22 | Ausgang | UART-Sendung zum Host (Host RX). Ruhepegel High, solange der Pico läuft. Der Host kann daran erkennen, dass der Pico versorgt ist |
| G18 | Eingang | I2S Bittakt (BCLK) vom Host |
| G19 | Eingang | I2S Wortauswahl (WS, LRCK) vom Host |
| G21 | Eingang | I2S Daten vom Host |
| G26 | Ausgang | S/PDIF (3,3 V) für einen TOSLINK-Sender, etwa Cliff FCR684208T an 3V3 und GND des Pico |
| G32, G33 (Lötpunkte unten) | I2C | SDA und SCL zum UKW-Modulator QN8007, 400 kHz, Pull-ups auf der Modulatorbaugruppe. Die Baugruppe hört an G18/G19/G21 parallel mit |
| G1, G3, G0, EN (kurze Reihe) | - | Programmieradapter: UART0 mit 115200 Baud für Flash und Log, G0 und EN für den Download-Modus |

Alle Pegel 3,3 V. Die drei I2S-Eingänge und G25 haben Pull-down, damit ein ausgeschalteter Host nicht über Pull-ups gespeist wird und offene Leitungen definiert Low liegen.

## I2S-Eingang

- Der Pico ist I2S-Slave: Der Host liefert Bittakt und Wortauswahl.
- Format Philips (I2S), zwei Slots zu 32 Bit, Stereo. Der Pico wertet die oberen 16 Bit jedes Slots aus. Ein 16-Bit-Host legt seine Abtastwerte also oben in den Slot.
- Quellrate 8000 bis 96000 Hz. Der Host meldet sie mit `RATE`. Außerdem misst der Pico die Rate jede Sekunde selbst und übernimmt eine abweichende Standardrate (8, 11,025, 12, 16, 22,05, 24, 32, 44,1, 48, 64, 88,2, 96 kHz), wenn sie zwei Fenster hintereinander klar messbar ist. Nach dem Start gilt 44100 Hz, bis eine Rate kommt.
- Bluetooth sendet immer mit 44,1 kHz, 16 Bit. Der Pico rechnet jede Quellrate um (Kaiser-Sinc-Tiefpass) und gleicht die Taktdrift über den Pufferstand aus. Der optische Ausgang läuft mit der Quellrate, verdoppelt unter 32 kHz und halbiert über 48 kHz. Der UKW-Modulator nimmt nur 32, 40, 44,1 und 48 kHz an und bleibt bei anderen Raten stumm.
- Jede neue Rate blendet aus, verwirft den Puffer und blendet wieder ein. Ratenwechsel mitten im Strom sind erlaubt.

## Serielle Schnittstelle

- 115200 Baud, 8N1, keine Flusssteuerung, ASCII.
- Eine Zeile ist ein Befehl oder eine Antwort und endet mit `\n`. Ein `\r` davor wird übergangen. Höchstens 127 Zeichen ohne Zeilenende. Leere Zeilen, längere Zeilen und Zeilen mit Steuerzeichen (auch das Nullbyte, das eine offene Leitung erzeugt) werden ohne Antwort verworfen.
- Befehle sind Großbuchstaben und werden exakt verglichen. Jeder Befehl bekommt genau eine Antwortzeile. Der Pico bedient die Schnittstelle alle 5 ms, eine Antwort kommt innerhalb weniger Millisekunden. Der Host sollte höchstens 300 ms warten.
- Der Pico fragt nie von sich aus. Unaufgefordert meldet er nur Ereignisse (unten).
- Adressen sind `AA:BB:CC:DD:EE:FF`, vom Pico in Großbuchstaben, an den Pico in Groß- oder Kleinschreibung.
- Namen und Texte stehen immer zuletzt in der Zeile. Sichtbares ASCII und Leerzeichen bleiben, jedes andere Byte (UTF-8) und `%` stehen als `%XX`, etwa `Kopfh%C3%B6rer`. Gerätenamen haben höchstens 30 Byte UTF-8, der Pico kürzt an einer Zeichengrenze.

### Befehle und Antworten

| Befehl | Antwort | Wirkung |
|---|---|---|
| `HALLO` | `HALLO STAND=5.3+<Commit>` | Firmwarestand: Nummer, `+` und der kurze Git-Commit des Builds, `m` dahinter bei nicht committeten Änderungen. Jederzeit, auch vor dem Start des Bluetooth-Stacks |
| `STATUS` | `STATUS <Zustand> <Adresse\|->[ <Name>]`, bei `VERBUNDEN` zusätzlich `KOPFHOERER <0..127>` | Zustand `STILL`, `SUCHT`, `VERBINDET`, `VERBUNDEN`. Adresse und Name der Gegenstelle bei `VERBINDET` und `VERBUNDEN`, sonst des gespeicherten Geräts, `-` ohne |
| `SUCHE` | `SUCHE LAEUFT`, dann je Gerät `TREFFER <Adresse> <RSSI\|?>[ <Name>]`, zuletzt `SUCHE ENDE <Anzahl>` | Suche nach Audiogeräten über 8,96 s (spätestens nach 9,96 s abgebrochen). Nur im Zustand `STILL`, sonst `FEHLER BELEGT`. Während einer laufenden Suche `SUCHE LAEUFT`. Je Adresse eine Zeile, eine zweite nur, wenn der Name erst später kommt. Höchstens 16 Geräte. RSSI in dBm, `?` ohne Angabe |
| `VERBINDE <Adresse>` | `VERBINDE LAEUFT`, dann `VERBUNDEN ...` oder `GETRENNT <Adresse>` | Ein Verbindungsversuch, nach 20 s ohne Ergebnis gescheitert. Während einer Suche bricht der Pico sie ab (`SUCHE ENDE`) und verbindet danach. Bei `VERBINDET` oder `VERBUNDEN` `FEHLER BELEGT`, bei falscher Adresse `FEHLER ADRESSE`. Eine erfolgreiche Verbindung wird gespeichert (Adresse, Name, Kopplung) |
| `TRENNE` | `TRENNE OK` | Beendet Suche (`SUCHE ENDE`), Versuche und Verbindung (`GETRENNT`). Das Gerät bleibt gespeichert. Bis zum nächsten `VERBINDE` oder Neustart nimmt der Pico keine eingehende Verbindung an |
| `VERGISS` | `VERGISS OK` oder `FEHLER SPEICHER` | Wie `TRENNE`, dazu alle Kopplungen und das gespeicherte Gerät samt Lautstärke löschen. Bei bestehender Verbindung erst nach der Meldung der Trennung, höchstens 3 s später. Bis dahin `SUCHE`, `VERBINDE` und `VERGISS` mit `FEHLER BELEGT` |
| `TESTTON` | `TESTTON LAEUFT` | 3 s lang 0,5 s Ton und 0,5 s Pause, 440 Hz, 5 % der Vollaussteuerung, anstelle des Tons. Nur bei `VERBUNDEN`, sonst `FEHLER NICHT_VERBUNDEN` |
| `STUMM AN` / `STUMM AUS` | gleichlautend | Ton an Bluetooth-Gerät, optischem Ausgang und UKW-Modulator über 10 ms aus- bzw. einblenden. Jede Verbindung und jede Trennung macht den Pico wieder stumm. Nach `VERBUNDEN` muss der Host `STUMM AUS` erneut senden |
| `RATE <Hz>` | gleichlautend oder `FEHLER RATE` | Quellrate 8000 bis 96000 |
| `LAUTSTAERKE <0..127>` | gleichlautend oder `FEHLER LAUTSTAERKE` | Gerätelautstärke per AVRCP (absolute Lautstärke). Ohne Verbindung für die nächste gemerkt, gespeichert nach 5 s ohne Änderung. Vorgabe 38 |
| `UKW` | `UKW <Zustand> <kHz> <Leistung> <Rate\|-> <Chip\|->` | Zustand des UKW-Modulators: `FEHLT`, `AUS`, `START`, `AN`, `STUMM`. Frequenz und Leistungsstufe. Quellrate am Modulator oder `-`. Chip `QN8007`, `QN8007L` oder `-` |
| `UKW AN` / `UKW AUS` | gleichlautend oder `FEHLER KEIN_UKW` | Sender ein- oder ausschalten. Nach `AN` ist er nach rund 0,7 s bereit |
| `UKW FREQ <kHz>` | gleichlautend, `FEHLER FREQ` oder `FEHLER KEIN_UKW` | Frequenz 76000 bis 108000 im 50-kHz-Raster. Wirkt sofort. Vorgabe 87500 |
| `UKW LEISTUNG <0..15>` | gleichlautend, `FEHLER LEISTUNG` oder `FEHLER KEIN_UKW` | Leistungsstufe: 0 ist die kleinste (101,5 dBµV am Antennenausgang), 15 die größte (124 dBµV), Schritt 1,5 dB. Vorgabe 0 |
| `RDS PS[ <Text>]` | `RDS PS OK`, `FEHLER TEXT` oder `FEHLER KEIN_UKW` | Programmname, kodiert wie Namen. Der Pico kürzt auf 8 Zeichen des RDS-Zeichensatzes. Ohne Text wird er gelöscht |
| `RDS RT[ <Text>]` | `RDS RT OK`, `FEHLER TEXT` oder `FEHLER KEIN_UKW` | Radiotext, bis 64 Zeichen. Jede Änderung kippt das A/B-Bit. Ohne Text keine Radiotextgruppen |
| `ZAEHLEN` | `ZAEHLEN LAEUFT` | Leitungstest: Flankenzähler an G18, G19 und G21 auf null setzen und starten |
| `ERGEBNIS` | `ERGEBNIS G18=<n> G19=<n> G21=<n>` | Leitungstest: Zähler anhalten und melden, der Host schaltet dazu eine Leitung nach der anderen mit 100 Flanken |
| jede andere Zeile | `FEHLER UNBEKANNT` | - |

Ohne erkannten UKW-Modulator speichert der Pico keine UKW- oder RDS-Werte. Der Host sendet sie erneut, sobald die UKW-Zeile einen Modulator meldet.

### Unaufgeforderte Zeilen

| Zeile | Wann |
|---|---|
| `TREFFER <Adresse> <RSSI\|?>[ <Name>]` | Während einer Suche je gefundenem Audiogerät |
| `SUCHE ENDE <Anzahl>` | Ende der Suche, auch nach Abbruch durch `TRENNE` oder `VERBINDE` |
| `VERBUNDEN <Adresse>[ <Name>]` | Nach jeder Verbindung, auch wenn das gespeicherte Gerät sich selbst verbindet. Ein zweites Mal, wenn der Pico den Namen erst beim Gerät erfragt hat |
| `GETRENNT <Adresse>` | Verbindung beendet oder letzter Versuch gescheitert |
| `KOPFHOERER <0..127>` | Lautstärke des verbundenen Geräts (der Name der Zeile stammt aus der Zeit, als nur Kopfhörer vorgesehen waren): nach jeder Verbindung, nach `STATUS` bei bestehender Verbindung und nach jeder Lautstärketaste am Gerät |
| `UKW ...` | Bei jedem Wechsel des UKW-Zustands, also nur mit erkanntem oder verschwundenem Modulator |

### Fehlerkennungen

`BELEGT`, `ADRESSE`, `NICHT_VERBUNDEN`, `SPEICHER`, `RATE`, `LAUTSTAERKE`, `KEIN_UKW`, `FREQ`, `LEISTUNG`, `TEXT`, `UNBEKANNT`. Immer als `FEHLER <Kennung>`.

## Verhalten

- **Ruhe:** Der Pico funkt nur auf Befehl. Er ist unsichtbar für Suchen. Solange ein Gerät gespeichert ist, er `STILL` ist und kein `TRENNE` es untersagt hat, ist er für dieses Gerät ansprechbar (Page-Scan): Ein Gerät, das nach einem Abriss oder Einschalten von sich aus anruft, wird angenommen. Jedes andere Gerät wird sofort wieder getrennt.
- **Start:** Rund 0,8 s nach dem Einschalten läuft der Bluetooth-Stack. Ist ein Gerät gespeichert, wartet der Pico 15 s ansprechbar auf ihn und wählt ihn dann bis zu dreimal an (je 20 s, 5 s Pause). Der letzte gescheiterte Versuch endet mit `GETRENNT`. Ohne gespeichertes Gerät bleibt er still.
- **Verbunden:** Der Pico sendet den I2S-Ton, bis zum ersten `STUMM AUS` Stille. 3 s nach der Verbindung schickt er die gespeicherte Lautstärke per AVRCP. Tasten am Gerät melden ihre Lautstärke als `KOPFHOERER`. Das Echo eines eben per `LAUTSTAERKE` gesetzten Werts meldet er binnen 1 s nicht zurück.
- **Optischer Ausgang:** immer aktiv, auch ohne Bluetooth-Gerät. Ohne Ton gehen Stille-Rahmen hinaus, damit der Empfänger eingerastet bleibt. Format IEC 60958 (Verbraucher), 16-Bit-Werte, Kanalstatus mit der Rate.
- **UKW-Modulator:** Beim Start und danach alle 10 s prüft der Pico, ob ein QN8007 an 0x2B, 0x2C oder 0x2A antwortet. Ohne Modulator bleibt es bei dieser Prüfung. Mit Modulator stellt er Register nach Datenblatt ein (74,5 kHz Hub, Pilot 9 %, 50 µs Pre-Emphasis, Stereo, RDS-Hub 2,8 kHz, 26-MHz-Quarz), sendet erst nach `UKW AN` und hält den RDS-Puffer mit Gruppen 0A (Programmname) und 2A (Radiotext) im Wechsel gefüllt (Programmkennung 0xD5A2, PTY 0). Drei Busfehler hintereinander gelten als verschwundener Modulator. Gesetzliche Grenzen der Sendeleistung sind Sache des Betreibers.
- **Wachhund:** Bleibt die Hauptschleife oder der I2S-Task 5 s hängen, startet der Pico neu. Eine Verbindung endet dann wie bei einem Stromausfall, das gespeicherte Gerät kommt von selbst zurück.
- **Speicher:** Kopplungsschlüssel, Adresse, Name und Lautstärke des Geräts liegen im NVS des Pico (Bereich `connected_bda`). UKW-Werte werden nicht gespeichert. Sie kommen bei jedem Start vom Host.

## Ablaufbeispiele

Gerät koppeln und Ton starten (Host-Zeilen mit `>`, Pico mit `<`, Beispiel mit einem Kopfhörer, jedes A2DP-Gerät verhält sich gleich):

```text
> HALLO
< HALLO STAND=5.3+1a2b3c4
> STATUS
< STATUS STILL -
> SUCHE
< SUCHE LAEUFT
< TREFFER AA:BB:CC:DD:EE:FF -49 Kopfh%C3%B6rer
< SUCHE ENDE 1
> VERBINDE AA:BB:CC:DD:EE:FF
< VERBINDE LAEUFT
< VERBUNDEN AA:BB:CC:DD:EE:FF Kopfh%C3%B6rer
< KOPFHOERER 38
> RATE 48000
< RATE 48000
> LAUTSTAERKE 60
< LAUTSTAERKE 60
> STUMM AUS
< STUMM AUS
```

Danach I2S mit 48 kHz liefern. Reihenfolge beim Host: `STUMM AN` vor einer neuen Rate oder Lautstärke, `STUMM AUS` danach, dann wird der Pico nie mit alten Werten laut.

UKW-Sender mit RDS einschalten:

```text
> UKW
< UKW AUS 87500 0 48000 QN8007
> UKW FREQ 101300
< UKW FREQ 101300
> UKW LEISTUNG 2
< UKW LEISTUNG 2
> RDS PS radioeins
< RDS PS OK
> RDS RT Nachrichten um 12 Uhr
< RDS RT OK
> UKW AN
< UKW AN
< UKW START 101300 2 48000 QN8007
< UKW STUMM 101300 2 48000 QN8007
> STUMM AUS
< STUMM AUS
< UKW AN 101300 2 48000 QN8007
```

## Log auf UART0

Über den Programmieradapter (115200 Baud) schreibt der Pico Infozeilen: `START` (Stand, Buildzeit, Neustartgrund), `CHIP`, `EIGEN` (eigener Name „RetroRadio “ plus die letzten drei Byte der Bluetooth-Adresse), `GESPEICHERT`, `SPEICHER`, `WACHHUND`, `UART <-`/`UART ->` je Zeile, `ZUSTAND`, `VERBINDUNG`, `SUCHE`, `AUDIO` und `OPTISCH` alle 10 s mit Rate, Füllstand, Korrektur und Last, `BETRIEB` alle 60 s mit Heap und Stackreserve, `UKW` und `RDS`. Dazwischen stehen die Zeilen der Bibliothek.

## Bauen und flashen

Voraussetzung ist PlatformIO, die Platform pioarduino 55.03.37 (Arduino-ESP32 3.3.7) lädt PlatformIO selbst. Die Bibliothek ESP32-A2DP 1.8.11 ist in `platformio.ini` auf ihren Commit festgelegt und bleibt unverändert, die Firmware leitet nur eine eigene Klasse ab. Im Hauptprojekt aufgerufen, mit dem Anschluss des Programmieradapters statt `/dev/ttyACM0`:

```bash
pio run -d bt-sender
pio run -d bt-sender -t upload --upload-port /dev/ttyACM0
pio device monitor -d bt-sender --port /dev/ttyACM0
```

Wer immer denselben Adapter benutzt, kann `upload_port` und `monitor_port` in einer eigenen, nicht versionierten Datei `bt-sender/platformio_lokal.ini` festlegen. `git_stand.py` hängt beim Build den Git-Commit an den Stand, ohne Git bleibt nur die Nummer. Ohne PlatformIO: Bootloader an `0x1000`, Partitionstabelle an `0x8000`, `boot_app0.bin` an `0xe000`, Firmware an `0x10000` (esptool, Chip esp32, DIO, 40 MHz, 4 MB). Beim Flashen bleibt das NVS mit der Kopplung erhalten.

Hängt der Pico an einem Host, der ihn mit 5 V versorgt: Programmieradapter nur anstecken, wenn die 5 V getrennt sind (Jumper), und erst den Adapter abziehen, dann die 5 V wieder anschließen.

## Dateien

- `src/main.cpp`: Steuerung, UART-Protokoll, I2S-Empfang, optischer Ausgang, Anbindung des UKW-Modulators.
- `include/BtAudio.h`: Ratenumrechnung, Driftausgleich, Ringpuffer, Ausgabestufe, S/PDIF-Kodierung.
- `include/BtFm.h`: Treiber des QN8007 und RDS-Gruppenbildung.
- `git_stand.py`, `platformio.ini`: Build.

Die Gegenseite des Protokolls im Hauptprojekt ist `include/BtProtocol.h`.

[RetroRadioTab5](../README.md) > [Dokumentation](../README.md#dokumentation) > [Bluetooth-Sender](../docs/Bluetooth-Sender.md) > Pico-Firmware
