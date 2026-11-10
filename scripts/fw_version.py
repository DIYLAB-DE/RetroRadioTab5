# Stellt die feste FW_VERSION und das Veröffentlichungsdatum FW_DATE für den Build bereit.
# Beide Angaben werden nur vor einem neuen Release aktualisiert.
# FW_VERSION: v<JJ>.<M>.<T>[.<R>]   zweistelliges Jahr, Monat und Tag ohne führende Null, optional eine feste Revision ab 1
# FW_DATE:    <TT>.<MM>.<JJJJ>

Import("env")
fw_version = "v26.11.10"
fw_date = "10.11.2026"

env.Append(CPPDEFINES=[
    ("FW_VERSION", env.StringifyMacro(fw_version)),
    ("FW_DATE", env.StringifyMacro(fw_date)),
])

print("FW_VERSION = {}".format(fw_version))
print("FW_DATE    = {}".format(fw_date))