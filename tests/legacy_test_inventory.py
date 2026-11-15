"""Check that the QML test split retains every photo service test."""
import os
from pathlib import Path
import subprocess
import sys


def functions(executable):
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", QML_DISABLE_DISK_CACHE="1")
    for name in ("DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH",
                 "DYLD_FALLBACK_FRAMEWORK_PATH", "LD_LIBRARY_PATH"):
        environment.pop(name, None)
    result = subprocess.run([str(Path(executable)), "-functions"], env=environment,
                            text=True, capture_output=True, check=True)
    return {line.strip() for line in result.stdout.splitlines() if line.strip().endswith("()")}


def main():
    if len(sys.argv) != 3:
        raise ValueError("Expected the service and legacy test executables")
    service, legacy = map(functions, sys.argv[1:])
    expected_views = {"chronologicalGalleryOpensAtNewestAndPreservesBrowsing()",
                      "galleryRendersAndLatePreviewKeepsBrowsingPosition()"}
    if not service or not service <= legacy or legacy - service != expected_views:
        raise AssertionError(f"Photo test inventory changed: missing service={service - legacy}, legacy-only={legacy - service}")


if __name__ == "__main__":
    main()
