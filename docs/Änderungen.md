# Änderungen

[RetroRadioTab5](../README.md) > [Dokumentation](../README.md#dokumentation) > Änderungen

Hier steht, was sich von Version zu Version ändert, die neueste Version steht oben. Jede Version erscheint als [Release auf GitHub](https://github.com/DIYLAB-DE/RetroRadioTab5/releases) mit eigener Nummer. Die Nummer zeigt das Tab5 unter Einstellungen -> Info & Neustart und die Weboberfläche im Abschnitt System. Sie ändert sich nur mit einem neuen Release, auch wenn Du die Firmware selbst baust.

## v26.11.10 vom 10.11.2026

Die zweite Vorabversion, [v26.11.10 auf GitHub](https://github.com/DIYLAB-DE/RetroRadioTab5/releases/tag/v26.11.10).

### Neu

- Neu: Mit angeschlossenem Akku zeigt das Tab5 in jeder Ansicht ein Akkusymbol mit fünf Segmenten (grün, bei zwei Segmenten gelb, bei einem rot). Im Hauptmenü und auf dem Sperrbildschirm steht die Prozentzahl daneben, in den Einstellungen darüber. Unter 10 % blinkt das rote Segment, ein Blitz zeigt Strom über USB. Die Prozentzahl ist aus der Akkuspannung geschätzt und zeigt beim Laden etwas zu viel. Ohne Akku erscheint kein Symbol und alle Ansichten bleiben wie bisher.
- Neu: Der Sekundenzeiger der Uhr im Hauptmenü schleicht in einer gleichmäßigen Bewegung, statt jede Sekunde zu springen. Wer die Sekundenschritte lieber mag, schaltet um: am Gerät unter Einstellungen -> Zeit & Zeitserver in der neuen Zeile „Sekundenzeiger“, in der Weboberfläche unter Systemeinstellungen mit dem Button neben „Sekundenzeiger“. Die Wahl gilt sofort, ohne Neustart, und bleibt gespeichert. Nach dem Update schleicht der Zeiger.

### Geändert

- Geändert: Im Radio zeigt der Button zum Zurücksetzen einen Kreispfeil statt des Wortes „reset“. Im SD-Player zeigen die Buttons für Dateiauswahl, Liste leeren und Zurücksetzen einen offenen Ordner, einen Papierkorb und einen Kreispfeil statt „durchs.“, „leeren“ und „reset“. Alle Buttons der unteren Reihe sind gleich breit, die Hinweise der leeren Titelliste nennen das Ordnersymbol.
- Geändert: Auf der Seite Zeit & Zeitserver stehen Datum und Uhrzeit links und „zeit setzen“ rechts daneben. In der Weboberfläche stehen „PIN-Sperre“ und „Sekundenzeiger“ mit ihren Buttons platzsparend in einer Zeile.
- Geändert: Erstinstallation und Fehlerhilfe erklären den Abbruch mit `resultcallback`. Er entsteht, wenn das Terminal ein veraltetes PlatformIO 4.3.4 aus den Paketquellen von Linux Mint oder Ubuntu startet statt des PlatformIO aus VS Code. Abhilfe ist das Terminal über **PlatformIO: New Terminal** oder der volle Pfad `~/.platformio/penv/bin/pio`.

### Behoben

- Behoben: Zerhackte Wiedergabe bei Streams ohne Vorlauf ([Issue 1](https://github.com/DIYLAB-DE/RetroRadioTab5/issues/1)). Ein Encoder im eigenen Netz, der in Echtzeit sendet, ließ dem Tab5 nach jedem Verbindungsaufbau nur rund 46 Millisekunden Reserve, jede kleine Verzögerung im WLAN wurde ein hörbarer Aussetzer. Die 18.000 Byte, ab denen ein Stream startet, sind jetzt ein echter Vorrat, bei 128 kbit/s rund 0,6 bis 0,8 Sekunden. Der Senderstart dauert dadurch nicht länger. Internetsender, die beim Verbinden einen Vorlauf schicken, waren nicht betroffen.
- Behoben: Jeder Start eines AAC-Senders ließ rund 7 KB Speicher belegt. Der AAC-Decoder gibt seine Tabellen beim Schließen jetzt frei.
- Behoben: Die Adresse von Radio Paradiso Berlin (Platz PARADISO) gibt es nicht mehr, das Radio meldete dort „Stream unterbrochen“. Die Werksliste enthält die neue Adresse `https://streams.paradiso.de/Berlin/mp3-192/radiode/`. Eine schon gespeicherte Senderliste behält die alte Adresse. Trage die neue dann in der Weboberfläche über den Sendermarker und **Bearbeiten** ein. **Reset** bei der Senderliste stellt ebenfalls die Werkssender her, überschreibt aber die eigene Liste.

## v26.11.8 vom 08.11.2026

Die erste öffentliche Version von RetroRadioTab5, [v26.11.8 auf GitHub](https://github.com/DIYLAB-DE/RetroRadioTab5/releases/tag/v26.11.8). Sie ist als Vorabversion gekennzeichnet, weil sie bisher nur an wenigen Geräten geprüft ist.

### Umfang

- **Hauptmenü:** große runde Uhr mit Datum und WLAN-Anzeige, darunter die Symbole für Radio, SD-Player und Einstellungen. Ein Sperrbildschirm mit PIN schützt das Gerät.
- **Radio:** Internetradio mit 70 Senderplätzen auf einer nachgebildeten Analogskala, Favoriten, vier magischen Augen, Lautstärke, Balance, Klangregler für Tiefen, Mitten und Höhen und Sleep-Timer. Im Stand zeigt das magische Auge die Stärke des WLAN-Empfangs.
- **Visualisierungen:** acht Ansichten in Radio und SD-Player, analoge und digitale VU-Meter, Spektrum, mittiges Spektrum, Wasserfall, Wellenform, Goniometer und Nordlicht.
- **SD-Player:** Musik von der microSD-Karte mit acht animierten Kassetten, Cover, Ordnerauswahl und Playlist.
- **Einstellungen:** WLAN, Bluetooth, UKW, Zeit, Zeitfenster, Sprache, PIN-Sperre, Klang, Anzeige, Thema, Bildschirm samt automatischer Drehung und Geräteinformationen.
- **Themen:** Retro mit den Farben des Originals, E-Paper in hellen Papiertönen und Individuell mit eigenen Farben aus der Weboberfläche.
- **Weboberfläche:** Fernbedienung, Senderpflege mit Sortieren, Sichern und Laden der Senderliste, Systemeinstellungen, Farben, Dateimanager und Bildschirmaufnahme im Browser. Für größere Dateien gibt es einen FTP-Zugang zur SD-Karte.
- **Bluetooth-Sender:** optionale Erweiterung mit einem M5Stamp Pico für Bluetooth-Kopfhörer und -Lautsprecher, mit optischem Ausgang und Ansteuerung eines UKW-Modulators mit RDS.
- **Hardware:** eine Firmware für alle drei Displayausführungen des Tab5. Der Ton kommt über den eingebauten Lautsprecher oder die Kopfhörerbuchse. Der Upload prüft die Chiprevision und erhält Sender und Einstellungen.

### Zuletzt hinzugekommen

- Neu: Die PIN-Sperre lässt sich am Gerät und in der Weboberfläche ein- und ausschalten, wie in RetroRadio v26.11.5. Ein gemeinsamer Schalter gilt für Radio und Weboberfläche. Ab Werk und nach dem Zurücksetzen auf Werkseinstellungen ist die Sperre eingeschaltet. Zum Ausschalten wird einmal die aktuelle PIN verlangt, Einschalten geht ohne, danach gilt wieder die bisherige PIN. Der Schalter wirkt sofort und bleibt über einen Neustart erhalten.
- Neu: Ohne PIN-Sperre öffnet das Tab5 beim Start und beim Aufwecken aus dem Zeitfenster direkt das Hauptmenü, die Geste in der linken Ecke sperrt nicht, und die Weboberfläche öffnet sich ohne PIN. Wird die Sperre ausgeschaltet, während der Sperrbildschirm offen ist, gibt er das Gerät sofort frei.
- Geändert: Die Seite Einstellungen -> Sicherheit hat oben die Zeile „PIN-Sperre“ mit Schalter und den Untertitel „Sperre für Radio und Weboberfläche“. In der Weboberfläche stehen der Schalter und „PIN ändern“ in einer Zeile unter Netzwerkname und Sprache. Ausschalten und PIN-Änderung laufen über einen kleinen Dialog, eine neue PIN gilt sofort ohne Neustart.
- Neu: Die Play-Taste des SD-Players wählt die Wiederholung. Kurz tippen während der Wiedergabe wiederholt den aktuellen Titel (Symbol grün), nochmals kurz tippen schaltet das wieder aus, ohne dass der Titel neu beginnt. Mindestens 0,8 Sekunden halten wiederholt die ganze Playlist (Symbol rot), auch aus dem Stopp oder der Pause heraus.
- Neu: Pause, Fortsetzen, Vor und Zurück und die Wahl eines Titels in der Liste behalten die Wiederholung. Stopp, Liste leeren und Verlassen des Players schalten sie aus. Die Wahl wird nicht gespeichert.
- Geändert: Die Kassette zeigt den Fortschritt der ganzen Playlist. Jeder Titel belegt denselben Bandanteil, am Ende der Liste ist das Band ganz rechts aufgewickelt. Der Fortschrittsbalken zeigt weiter den einzelnen Titel.

[RetroRadioTab5](../README.md) > [Dokumentation](../README.md#dokumentation) > Änderungen
