from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class QtCompatibilityCTestContract(unittest.TestCase):
    def test_cases_are_registered_without_qttest(self):
        source = (ROOT / "tests/photo_qt_compatibility.cpp").read_text()
        for case in (
            "existingServiceChangesAndRetires",
            "previewBecomesVisibleOnlyWhenReady",
            "bridgeMayRetireDuringDeferredReconciliation",
        ):
            self.assertIn(f'runner.run("{case}"', source)
        self.assertNotRegex(source, r"\b(?:QTest|QSignalSpy|QAbstractItemModelTester)\b|\bQTEST_|<QtTest|<QTest>|\bQ(?:VERIFY|COMPARE|TRY|FETCH|SKIP|FAIL)\w*")

    def test_build_and_installed_consumer_do_not_link_qttest(self):
        cmake = (ROOT / "src/Presentation/QtCompatibility/CMakeLists.txt").read_text()
        link = re.search(r"target_link_libraries\(iiPhotoLibraryQtPresentationBridgeTests\s+([^)]*)\)", cmake)
        self.assertIsNotNone(link)
        self.assertNotIn("Qt6::Test", link.group(1))
        self.assertNotRegex(cmake, r"find_package\(Qt6[^)]*\bTest\b")
        consumer = (ROOT / "tests/qt-presentation-consumer/CMakeLists.txt").read_text()
        self.assertNotIn("Qt6::Test", consumer)
        self.assertNotRegex(consumer, r"find_package\(Qt6[^)]*\bTest\b")


if __name__ == "__main__":
    unittest.main()
