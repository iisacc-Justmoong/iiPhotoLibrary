from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PresentationBridgeCTestContract(unittest.TestCase):
    def test_source_retains_each_case_without_qttest(self):
        source = (ROOT / "tests/photo_presentation_bridge.cpp").read_text()
        for case in (
            "eventsAndLifetime",
            "bridgeCanBeRetiredDuringRowNotification",
            "recoversWhenAChangeWasMissed",
            "bridgeCanBeRetiredDuringStructuralChange",
        ):
            self.assertIn(f'runner.run("{case}"', source)
        self.assertNotRegex(source, r"\bQTest\b|\bQTEST_|<QTest>")

    def test_both_consumers_link_without_qttest(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        target = re.search(
            r"target_link_libraries\(iiPhotoLibraryPresentationBridgeTests\s+([^)]*)\)",
            cmake,
            re.DOTALL,
        )
        self.assertIsNotNone(target)
        self.assertNotIn("Qt6::Test", target.group(1))
        consumer = (ROOT / "tests/bridge-consumer/CMakeLists.txt").read_text()
        self.assertNotIn("Qt6::Test", consumer)
        self.assertNotRegex(consumer, r"find_package\(Qt6[^)]*\bTest\b")


if __name__ == "__main__":
    unittest.main()
