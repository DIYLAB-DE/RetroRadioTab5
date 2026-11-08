#!/usr/bin/env python3
"""
Konvertiert webui/index.html -> include/index_html_gz.h (PROGMEM Byte-Array)

Aufruf:
  python3 scripts/generate_html_header.py

Oder als PlatformIO pre-build Script in platformio.ini:
  extra_scripts = pre:scripts/generate_html_header.py

Ein vorhandener Header bleibt unberührt, wenn sein Array zum HTML entpackt und
er sonst genau dem Generatorformat entspricht. Verschiedene zlib-Fassungen
(etwa zlib-ng) komprimieren denselben Inhalt anders, ohne diesen Vergleich
wäre der versionierte Header nach jedem Build geändert.
"""

import gzip
import os
import re
import sys
import zlib

# Unter PlatformIO/SCons steht env nach dem Import zur Verfügung. Daraus lässt
# sich die Projektwurzel zuverlässig ableiten, egal in welchem Unterordner
# dieses Script liegt. Beim direkten Aufruf bleibt _PIO_BUILD False.
_PIO_BUILD = False
try:
    Import("env")
    _PIO_BUILD = True
except Exception:
    pass


def resolve_project_dir():
    """Liefert das Projekt-Wurzelverzeichnis, in dem webui/ und include/ liegen."""
    if _PIO_BUILD:
        return env.subst("$PROJECT_DIR")
    # Standalone: eine Ebene über dem scripts-Ordner, sonst das aktuelle Verzeichnis.
    try:
        here = os.path.dirname(os.path.abspath(__file__))
    except NameError:
        return os.getcwd()
    parent = os.path.dirname(here)
    return parent if os.path.isdir(os.path.join(parent, "webui")) else here


PROJECT_DIR = resolve_project_dir()
INPUT_FILE = os.path.join(PROJECT_DIR, "webui", "index.html")
OUTPUT_FILE = os.path.join(PROJECT_DIR, "include", "index_html_gz.h")


def render_header(raw_size, compressed):
    """Liefert den Headertext für die komprimierten Bytes, reines ASCII mit LF."""
    gz_size = len(compressed)
    saving = 100 - (gz_size * 100 // raw_size)
    lines = [
        "#ifndef INDEX_HTML_GZ_H\n",
        "#define INDEX_HTML_GZ_H\n\n",
        "#include <pgmspace.h>\n\n",
        "/**\n",
        " * @brief Komprimierte WebUI (index.html) als PROGMEM-Array\n",
        " *\n",
        " * Auto-generiert von generate_html_header.py\n",
        " * NICHT manuell bearbeiten!\n",
        " *\n",
        f" * Original:     {raw_size:>7} Bytes\n",
        f" * Komprimiert:  {gz_size:>7} Bytes ({saving}% Ersparnis)\n",
        " */\n\n",
        f"static const size_t INDEX_HTML_GZ_SIZE = {gz_size};\n\n",
        "static const uint8_t INDEX_HTML_GZ[] PROGMEM = {\n",
    ]

    for i in range(0, gz_size, 16):
        chunk = compressed[i : i + 16]
        hex_str = ", ".join(f"0x{b:02X}" for b in chunk)
        if i + 16 < gz_size:
            lines.append(f"    {hex_str},\n")
        else:
            lines.append(f"    {hex_str}\n")

    lines.append("};\n\n")
    lines.append("#endif // INDEX_HTML_GZ_H\n")
    return "".join(lines)


def parse_header_payload(text):
    """
    Liest das Gzip-Array aus einem Headertext.

    Gibt die Bytes zurück oder None, wenn Array oder INDEX_HTML_GZ_SIZE fehlen,
    ein Eintrag kein 0xHH ist oder die Länge nicht zur Größenangabe passt.
    """
    size = re.search(r"INDEX_HTML_GZ_SIZE = (\d+);", text)
    array = re.search(r"INDEX_HTML_GZ\[\] PROGMEM = \{(.*?)\};", text, re.S)
    if not size or not array:
        return None
    tokens = array.group(1).replace(",", " ").split()
    if not all(re.fullmatch(r"0x[0-9A-F]{2}", token) for token in tokens):
        return None
    payload = bytes(int(token, 16) for token in tokens)
    return payload if len(payload) == int(size.group(1)) else None


def header_is_current(header_path, raw):
    """
    Prüft, ob der vorhandene Header unverändert bleiben kann.

    Wahr nur, wenn die Datei lesbar ist, ihr Array zu raw entpackt und sie Byte
    für Byte dem Generatorformat für genau dieses Array entspricht.
    """
    try:
        with open(header_path, "rb") as f:
            existing = f.read()
        payload = parse_header_payload(existing.decode("ascii"))
    except (OSError, UnicodeDecodeError):
        return False
    if payload is None:
        return False
    try:
        if gzip.decompress(payload) != raw:
            return False
    except (OSError, EOFError, zlib.error):
        return False
    return existing == render_header(len(raw), payload).encode("ascii")


def generate_header(input_path, output_path):
    """Liest HTML und schreibt den C-Header nur bei geändertem Inhalt oder Format neu."""

    if not os.path.exists(input_path):
        print(f"[WebUI] Quelldatei nicht gefunden: {input_path}")
        return False

    # Lesen
    with open(input_path, "rb") as f:
        raw = f.read()

    if header_is_current(output_path, raw):
        print(f"[WebUI] Inhalt unverändert, Header bleibt: {output_path}")
        return True

    # Komprimieren
    compressed = gzip.compress(raw, compresslevel=9, mtime=0)
    raw_size = len(raw)
    gz_size = len(compressed)
    saving = 100 - (gz_size * 100 // raw_size)

    # Header schreiben
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    with open(output_path, "wb") as out:
        out.write(render_header(raw_size, compressed).encode("ascii"))

    print(f"[WebUI] {raw_size:,} -> {gz_size:,} Bytes ({saving}% Ersparnis) -> {output_path}")
    return True


# Ausführung
if _PIO_BUILD:
    # Als pre:-Script direkt ausführen, nicht als deferred Action.
    generate_header(INPUT_FILE, OUTPUT_FILE)
elif __name__ == "__main__":
    inp = sys.argv[1] if len(sys.argv) > 1 else INPUT_FILE
    outp = sys.argv[2] if len(sys.argv) > 2 else OUTPUT_FILE
    if generate_header(inp, outp):
        print("[WebUI] Fertig!")
    else:
        sys.exit(1)