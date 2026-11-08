"""Geprüfter Upload auf ein M5Stack Tab5 über den USB-Serial-JTAG des ESP32-P4. Umgestellte Geräte behalten Bootloader, NVS, C6-Abbild und Senderdaten. Werksgeräte werden nur mit TAB5_ERSTFLASH=1 und nach einer Sicherung des ganzen Flash eingerichtet."""

import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time

ESPRESSIF_VID = 0x303A  # USB-Serial-JTAG des ESP32-P4, der CH9102F-Adapter des Bluetooth-Pico (1a86) hat andere Kennungen
USB_JTAG_PID = 0x1001
BOOTLOADER_OFFSET = 0x2000  # Die ersten zwei Sektoren des ESP32-P4 sind reserviert
TABLE_OFFSET = 0x8000
FLASH_SIZE = 0x1000000
SECTOR_SIZE = 0x1000
LEGACY_APP = (0, 0, 0x10000, 0x300000, 0)
WEB_APP = (0, 0, 0x520000, 0xAE0000, 0)
DATA_PARTITIONS = {"nvs": (1, 2, 0x9000, 0x6000, 0), "phy_init": (1, 1, 0xF000, 0x1000, 0), "slave_fw": (1, 0x40, 0x310000, 0x200000, 0), "stations": (1, 0x80, 0x510000, 0x10000, 0)}
FIRST_FLASH_VARIABLE = "TAB5_ERSTFLASH"
FIRST_FLASH_ERASE = ("nvs", "stations")  # Werksreste, die die Firmware beim ersten Start nicht als eigene Daten deuten soll


def tab5_ports():
    """Serielle Anschlüsse mit Espressif-USB-Serial-JTAG, sortiert. Nur dort hängt ein Tab5."""
    from serial.tools import list_ports
    return sorted(port.device for port in list_ports.comports() if port.vid == ESPRESSIF_VID and port.pid == USB_JTAG_PID)


def select_port(requested):
    """Wählt den Anschluss: ein angegebener Name oder Pfad muss zu einem angeschlossenen Tab5 führen, ohne Angabe muss genau eines angeschlossen sein.

    Angegeben werden kann der Gerätename aus der Portliste (Windows COM5, macOS /dev/cu.usbmodem…, Linux /dev/ttyACM0) oder ein Link darauf wie unter /dev/serial/by-id/. Unter Windows zählt die Groß- und Kleinschreibung nicht.
    """
    ports = tab5_ports()
    if requested:
        known = {os.path.normcase(port) for port in ports}
        if not {os.path.normcase(requested), os.path.normcase(os.path.realpath(requested))} & known:
            raise ValueError(f"{requested} ist kein angeschlossenes Tab5 (USB-Serial-JTAG {ESPRESSIF_VID:04x}:{USB_JTAG_PID:04x}).")
        return requested
    if len(ports) == 1:
        return ports[0]
    if not ports:
        raise ValueError(f"Kein Tab5 am USB gefunden (USB-Serial-JTAG {ESPRESSIF_VID:04x}:{USB_JTAG_PID:04x}).")
    raise ValueError("Mehrere Tab5 am USB, bitte eines angeben mit --upload-port <Pfad>: " + ", ".join(ports))


def tab5_port():
    """Anschluss für die Hilfsskripte: Umgebungsvariable TAB5_PORT, sonst das einzige angeschlossene Tab5. Beendet das Skript mit Meldung, wenn keines oder mehrere angeschlossen sind."""
    try:
        return select_port(os.environ.get("TAB5_PORT"))
    except ValueError as error:
        raise SystemExit(str(error))


def esptool_command(objcopy):
    """Befehl für esptool aus $OBJCOPY von pioarduino: ein einzelner Programmpfad, bei Leerzeichen in doppelten Anführungszeichen.

    Nicht wie eine Shellzeile zerlegen, weil Windows-Pfade Backslashes enthalten (C:\\Users\\…\\penv\\Scripts\\esptool.exe). @return Liste mit dem Pfad für subprocess.
    """
    path = objcopy.strip()
    if len(path) >= 2 and path[0] == path[-1] == '"':
        path = path[1:-1]
    if not path:
        raise ValueError("Kein esptool-Pfad ($OBJCOPY) von PlatformIO erhalten.")
    return [path]


def known_layout(table):
    """Erkennt ausschließlich die zwei freigegebenen Tabellen mit identischen Datenbereichen."""
    return set(table) == {*DATA_PARTITIONS, "factory"} and all(table.get(name) == item for name, item in DATA_PARTITIONS.items()) and table.get("factory") in (LEGACY_APP, WEB_APP)


def partitions(data):
    """Liest eine ESP-IDF-Tabelle und prüft Grenzen sowie die enthaltene MD5-Summe."""
    result = {}
    for position in range(0, len(data) - 31, 32):
        entry = data[position:position + 32]
        magic, kind, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", entry)
        if magic == 0xEBEB:
            if entry[16:] != hashlib.md5(data[:position]).digest():
                raise ValueError("Prüfsumme der Partitionstabelle stimmt nicht.")
            if not result:
                raise ValueError("Leere Partitionstabelle.")
            return result
        if magic != 0x50AA:
            raise ValueError("Partitionstabelle fehlt, ist unvollständig oder enthält keine Prüfsumme.")
        name = label.split(b"\0", 1)[0].decode("ascii")
        if name in result or size == 0 or offset < TABLE_OFFSET + SECTOR_SIZE or offset + size > FLASH_SIZE:
            raise ValueError("Ungültige Partitionsgrenzen oder doppelter Name.")
        for previous in result.values():
            if offset < previous[2] + previous[3] and previous[2] < offset + size:
                raise ValueError("Partitionen überschneiden sich.")
        result[name] = (kind, subtype, offset, size, flags)
    raise ValueError("Partitionstabelle ohne Abschluss.")


def describe(table):
    """Fasst eine Gerätetabelle für Meldungen zusammen."""
    if table is None:
        return "Tabelle nicht lesbar"
    return ", ".join(f"{name}@{offset:#x}/{size:#x}" for name, (kind, subtype, offset, size, flags) in table.items())


def validate_upload(old, new, images):
    """Erlaubt nur die freigegebene Web-App und Tabelle, Bootloader und alle Daten bleiben unangetastet."""
    if not known_layout(old):
        raise ValueError("Gerätetabelle weicht vom geprüften Tab5-Layout ab. Upload abgebrochen.")
    for name, item in old.items():
        if item[0] == 1 and new.get(name) != item:
            raise ValueError(f"Vorhandene Datenpartition {name} würde verändert.")
    factory = new.get("factory")
    if not known_layout(new) or factory != WEB_APP:
        raise ValueError("Unerwartete Factory-App-Partition.")
    expected = {TABLE_OFFSET: "partitions.bin", factory[2]: "firmware.bin"}
    if len(images) != len(expected):
        raise ValueError("Unerwartete Anzahl Flashdateien.")
    written = []
    for offset, path in images:
        path = Path(path)
        if expected.get(offset) != path.name:
            raise ValueError("Unerwartete Flashdatei oder Flashadresse.")
        size = path.stat().st_size
        limit = TABLE_OFFSET + SECTOR_SIZE if offset == TABLE_OFFSET else factory[2] + factory[3]
        begin = offset // SECTOR_SIZE * SECTOR_SIZE
        end = (offset + size + SECTOR_SIZE - 1) // SECTOR_SIZE * SECTOR_SIZE
        if size == 0 or end > limit:
            raise ValueError(f"{path.name} überschreitet den vorgesehenen Flashbereich.")
        if any(begin < other_end and other_begin < end for other_begin, other_end in written):
            raise ValueError("Flashdateien überschneiden sich.")
        written.append((begin, end))
        for name, item in old.items():
            if item[0] == 1 and begin < item[2] + item[3] and item[2] < end:
                raise ValueError(f"Upload würde Datenpartition {name} überschreiben.")


def validate_first_flash(new, images, table, app, bootloader):
    """Erlaubt für ein Werksgerät nur unser Layout mit der Web-App. Bootloader, Tabelle und App müssen in ihre Bereiche passen."""
    if not known_layout(new) or new.get("factory") != WEB_APP:
        raise ValueError("Unerwartete Factory-App-Partition.")
    expected = {TABLE_OFFSET: "partitions.bin", WEB_APP[2]: "firmware.bin"}
    if len(images) != len(expected) or any(expected.get(offset) != Path(path).name for offset, path in images):
        raise ValueError("Unerwartete Flashdatei oder Flashadresse.")
    if bootloader is None:
        raise ValueError("Für die Ersteinrichtung fehlt bootloader.bin.")
    for offset, path, limit in ((BOOTLOADER_OFFSET, Path(bootloader), TABLE_OFFSET - BOOTLOADER_OFFSET), (TABLE_OFFSET, table, SECTOR_SIZE), (WEB_APP[2], app, WEB_APP[3])):
        if not path.is_file() or not 0 < path.stat().st_size <= limit:
            raise ValueError(f"{path.name} fehlt oder überschreitet den Bereich ab {offset:#x}.")


def image_revisions(header):
    """Liest aus dem ESP-IDF-Abbildkopf die zulässige Chiprevision als Hundertstel, etwa (1, 199) für v0.01 bis v1.99."""
    if len(header) < 19 or header[0] != 0xE9:
        raise ValueError("firmware.bin beginnt nicht mit einem ESP-IDF-Abbildkopf.")
    return struct.unpack_from("<HH", header, 15)


def revision_text(revision):
    """Schreibt eine Chiprevision in Hundertsteln als v<major>.<minor>."""
    return f"v{revision // 100}.{revision % 100}"


def chip_info(base):
    """Fragt Chip, Revision und MAC ab, ohne das Gerät neu zu starten. @return (Ausgabe, Revision in Hundertsteln oder None, MAC ohne Doppelpunkte oder "unbekannt")."""
    text = subprocess.run([*base, "--after", "no-reset", "chip-id"], check=True, capture_output=True, text=True).stdout
    revision = re.search(r"revision v(\d+)\.(\d+)", text)
    mac = re.search(r"MAC:\s*([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})", text)
    return text, int(revision[1]) * 100 + int(revision[2]) if revision else None, mac[1].replace(":", "").lower() if mac else "unbekannt"


def check_revision(app, revision):
    """Bricht ab, wenn die Firmware nicht für die Chiprevision des Geräts gebaut ist. Der Bootloader würde sie sonst verweigern."""
    with Path(app).open("rb") as image:
        minimum, maximum = image_revisions(image.read(24))
    if revision is None:
        raise ValueError("Chiprevision nicht lesbar. Upload abgebrochen.")
    if not minimum <= revision <= maximum:
        raise ValueError(f"Chiprevision {revision_text(revision)} liegt außerhalb von {revision_text(minimum)} bis {revision_text(maximum)}, wofür diese Firmware gebaut ist (chip_variant in boards/retroradio-tab5.json).")


def backup_flash(base, root, mac, info):
    """Sichert den ganzen Flash eines Werksgeräts nach root/werk_<MAC>/ mit chip_id.txt und SHA256SUMS, dauert einige Minuten. @return Sicherungsverzeichnis."""
    directory = Path(root) / f"werk_{mac}"
    if directory.exists():
        directory = directory.with_name(f"{directory.name}_{time.strftime('%Y%m%d-%H%M%S')}")
    directory.mkdir(parents=True)
    image = directory / "flash_16MiB.bin"
    subprocess.run([*base, "--after", "no-reset", "read-flash", "0x0", hex(FLASH_SIZE), str(image)], check=True)
    if not image.is_file() or image.stat().st_size != FLASH_SIZE:
        raise ValueError("Flash-Sicherung unvollständig, nichts geschrieben.")
    (directory / "chip_id.txt").write_text(info)
    (directory / "SHA256SUMS").write_text(f"{hashlib.sha256(image.read_bytes()).hexdigest()}  {image.name}\n")
    return directory


def flash(esptool, port, images, bootloader=None, first_flash=False, recovery_root=".pio/recovery"):
    """Prüft Anschluss, Chiprevision und Gerätetabelle. Umgestellte Geräte: App schreiben und verifizieren, erst danach die Tabelle umstellen. Werksgeräte nur mit first_flash: ganzen Flash sichern, NVS und Senderdaten löschen, dann Bootloader, Tabelle und App schreiben."""
    port = select_port(port)
    base = [*esptool, "--chip", "esp32p4", "--port", port, "--baud", "921600"]
    table = next(Path(path) for offset, path in images if offset == TABLE_OFFSET)
    app = next(Path(path) for offset, path in images if offset == WEB_APP[2])
    new = partitions(table.read_bytes())
    info, revision, mac = chip_info(base)
    print(info.strip(), flush=True)
    check_revision(app, revision)
    options = ["write-flash", "--flash-mode", "qio", "--flash-freq", "80m", "--flash-size", "16MB"]
    with tempfile.TemporaryDirectory(prefix="tab5-partitionen-") as temporary:
        before = Path(temporary) / "partitions.bin"
        subprocess.run([*base, "--after", "no-reset", "read-flash", hex(TABLE_OFFSET), hex(SECTOR_SIZE), str(before)], check=True)
        try:
            old = partitions(before.read_bytes())
        except ValueError:
            old = None
    if old is not None and known_layout(old):
        validate_upload(old, new, images)
        print(f"Tab5 geprüft: App {app.stat().st_size} Bytes, Reserve {WEB_APP[3] - app.stat().st_size} Bytes. Bootloader, alte App und sämtliche Daten bleiben erhalten.", flush=True)
        subprocess.run([*base, "--after", "no-reset", *options, hex(WEB_APP[2]), str(app)], check=True)
        subprocess.run([*base, "--after", "no-reset", "verify-flash", hex(WEB_APP[2]), str(app)], check=True)
        subprocess.run([*base, "--after", "hard-reset", *options, hex(TABLE_OFFSET), str(table)], check=True)
        return
    if not first_flash:
        raise ValueError(f"Gerätetabelle ist nicht das RetroRadioTab5-Layout ({describe(old)}). Ein Werksgerät wird einmalig mit {FIRST_FLASH_VARIABLE}=1 eingerichtet, vorher wird der ganze Flash gesichert.")
    validate_first_flash(new, images, table, app, bootloader)
    backup = backup_flash(base, recovery_root, mac, info)
    print(f"Werksgerät gesichert unter {backup}. NVS und Senderdaten werden gelöscht, dann Bootloader, Tabelle und App geschrieben.", flush=True)
    for name in FIRST_FLASH_ERASE:
        subprocess.run([*base, "--after", "no-reset", "erase-region", hex(new[name][2]), hex(new[name][3])], check=True)
    subprocess.run([*base, "--after", "no-reset", *options, hex(BOOTLOADER_OFFSET), str(bootloader), hex(TABLE_OFFSET), str(table), hex(WEB_APP[2]), str(app)], check=True)
    subprocess.run([*base, "--after", "hard-reset", "verify-flash", hex(WEB_APP[2]), str(app)], check=True)
