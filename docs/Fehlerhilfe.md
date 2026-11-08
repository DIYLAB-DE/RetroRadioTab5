# Fehlerhilfe

[RetroRadioTab5](../README.md) > [Dokumentation](../README.md#dokumentation) > Fehlerhilfe

| Beobachtung | Prüfen und beheben |
| --- | --- |
| Kein serieller Anschluss | Datenkabel statt Ladekabel verwenden und das Tab5 möglichst direkt am Rechner anschließen. Unter Linux die Geräteberechtigungen prüfen, siehe [Erstinstallation](RetroRadioTab5-Ersteinrichtung.md#11-hilfe-bei-problemen). |
| Upload meldet **Gerätetabelle ist nicht das RetroRadioTab5-Layout** | Das Tab5 trägt noch die Werksfirmware. Den ersten Upload einmalig mit `TAB5_ERSTFLASH=1` ausführen, wie in der [Erstinstallation](RetroRadioTab5-Ersteinrichtung.md#7-retroradiotab5-erstmals-übertragen) beschrieben. |
| Upload bricht wegen der Chiprevision ab | Das Tab5 hat einen ESP32-P4 ab Revision 3.0 und wird derzeit nicht unterstützt. Auf das Gerät wurde nichts geschrieben. |
| Upload-Port belegt | Serielle Monitore und andere Programme schließen, die denselben Anschluss geöffnet haben. Hängen mehrere Tab5 am Rechner, den Anschluss ausdrücklich wählen. |
| WLAN bleibt getrennt | 2,4-GHz-Netz, Passwort und aktiviertes Profil prüfen. **test** ausführen, speichern und den angebotenen Neustart durchführen. Nach einem Test mit falschem Passwort weisen manche Router das Gerät einige Minuten lang ab. |
| Radiosymbol ist gesperrt | Ohne WLAN-Verbindung lässt sich das Radio nicht öffnen. WLAN einrichten, der SD-Player geht auch ohne. |
| Browser erreicht das Tab5 nicht | Die aktuelle IP-Adresse am Gerät ablesen und mit `http://` öffnen. Rechner und Tab5 müssen sich erreichen können, Gastnetz oder Client-Isolation können das verhindern. |
| Radiosteuerung fehlt im Browser | Das Radio am Gerät öffnen und die Weboberfläche entsperren. |
| Sender bleibt stumm | Anderen Sender testen, Stummschaltung und Lautstärke prüfen. Unter **Einstellungen -> Klang** muss **Lautsprecher** auf **An** stehen. Eine direkte Audio-Stream-Adresse verwenden, eine Webseite ersetzt keinen Stream. Bei HTTPS zusätzlich die Uhrzeit prüfen. |
| Lautsprecher schweigt, obwohl die Wiedergabe läuft | Steckt ein Kopfhörer oder ist ein Bluetooth-Gerät verbunden, schweigt der Lautsprecher absichtlich. |
| Infozeile zeigt **Stream unterbrochen** | Das Radio versucht den Sender selbst neu zu starten. Bleibt es dabei, WLAN-Empfang und Stream-Adresse prüfen. |
| SD-Player meldet **Keine SD-Karte** oder **SD-Karte nicht lesbar** | Sitz der Karte prüfen und das Tab5 neu starten. Die Karte muss mit FAT32 formatiert sein, exFAT wird nicht gelesen. Die Karte am Rechner prüfen, RetroRadioTab5 formatiert sie nie. |
| SD-Player meldet **Anforderung nicht angenommen** | Dateimanager oder FTP greifen gerade auf die Karte zu. Übertragung abwarten und erneut starten. |
| SD-Dateien fehlen | Im Web-Dateimanager kontrollieren, ob die Dateien auf der Karte sichtbar sind. Unterstützt werden MP3, AAC, M4A, FLAC und OGG. |
| Cover fehlt | Ein `folder.jpg` in den Musikordner legen oder eine Datei mit eingebettetem Cover verwenden. Die [Bildgrenzen](Radio-und-Musik.md#ordnercover-und-bildgrenzen) beachten. |
| FTP-Programm verbindet nicht oder bricht ab | FTP in der Weboberfläche einschalten, nach jedem Neustart ist es aus. Passiven Modus, unverschlüsseltes FTP und eine Verbindung zur Zeit einstellen. Hochladen geht nur, solange kein Radio läuft. |
| PIN vergessen | Ohne die aktuelle PIN lässt sich die Sperre weder am Gerät noch in der Weboberfläche aufheben, auch ein Firmware-Update ändert die PIN nicht. Notiere Dir eine geänderte PIN deshalb an einem sicheren Ort. |
| Bild steht auf dem Kopf | Unter **Einstellungen -> Bildschirm** die **Automatik** einschalten oder eine feste Ausrichtung wählen. |
| Uhrzeit falsch | Zeitzone, Zeitserver und letzten Abgleich unter **Einstellungen -> Zeit & Zeitserver** beziehungsweise **Info & Neustart** prüfen. |
| Bluetooth: **Sender** zeigt **nicht angeschlossen** | Verdrahtung nach dem [Plan](Bluetooth-Sender.md#verdrahtung) prüfen, besonders Masse, Kontakt 15 und 16 und den Jumper in der 5-V-Leitung. Der Programmieradapter darf nicht stecken. Danach das Tab5 neu starten. |
| Bluetooth: **Keine Verbindung** | Gerät einschalten und in Kopplungsbereitschaft bringen, erneut suchen. Ist das Gerät noch mit einem Telefon verbunden, diese Verbindung vorher trennen. |
| Änderungen scheinbar verloren | Bei Einstellungen die zugehörige **speichern**-Taste verwenden. Ein Neustart ohne vorheriges Speichern übernimmt noch nicht gespeicherte Eingaben nicht. |

Für einen Fehlerbericht die Ausführung des Tab5 (Aufkleber auf der Rückseite), die Firmwareversion unter **Einstellungen -> Info & Neustart**, die betroffene Anwendung und eine kurze reproduzierbare Schrittfolge angeben. Screenshots helfen bei Darstellungsfehlern. Netzwerkadressen, WLAN-Namen und Zugangsdaten vorher unkenntlich machen. Die Diagnosemeldungen schaltest Du unter **Einstellungen -> Allgemein -> Ausgabe über USB** ein. Ein Zurücksetzen auf Werkseinstellungen ist kein allgemeiner erster Lösungsversuch.

[Erstinstallation](RetroRadioTab5-Ersteinrichtung.md) | [Bedienung](Bedienung.md) | [Weboberfläche](Weboberfläche.md)

[RetroRadioTab5](../README.md) > [Dokumentation](../README.md#dokumentation) > Fehlerhilfe
