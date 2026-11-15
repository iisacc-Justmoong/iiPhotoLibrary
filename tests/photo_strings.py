import json
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
view = (root / "src/Views/PhotosView.lvui").read_text(encoding="utf-8")
used = set(re.findall(r'localized\("([^"]+)"\)', view))
if not used:
    raise SystemExit("The photo view has no localized strings")
# The host publishes picker failures through the view model's status field.
# Keep this host-originated message in the same catalog as the photo view.
used.add("photo.picker_failed")

def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate catalog key: {key}")
        result[key] = value
    return result

for locale, language in (("en", "en"), ("ko", "ko-KR")):
    path = root / "resources/photo-view/strings" / f"{locale}.json"
    catalog = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique)
    if catalog.keys() != {"locale", "strings"} or catalog["locale"] != language:
        raise SystemExit(f"Invalid photo catalog envelope: {path}")
    strings = catalog["strings"]
    if set(strings) != used or any(not isinstance(value, str) or not value for value in strings.values()):
        raise SystemExit(f"Photo catalog does not exactly cover the view: {path}")
    for key in ("photo.field_type", "photo.field_size", "photo.field_created",
                "photo.unit_bytes", "photo.unit_kb", "photo.unit_mb", "photo.unit_gb"):
        if "{value}" not in strings[key]:
            raise SystemExit(f"Photo formatting placeholder is missing from {key}: {path}")
