from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

class GalleryCTestContract(unittest.TestCase):
    def test_cases_keep_flat_model_contract(self):
        source = (ROOT / "tests/gallery_model.cpp").read_text()
        for case in ("incrementalChangesPreserveIdentity", "ambiguousIdentitiesResetSafely"):
            self.assertIn(f'runner.run("{case}"', source)
        self.assertGreaterEqual(source.count("FlatModelContract contract(&model)"), 2)
        self.assertGreaterEqual(source.count("contract.good()"), 2)
        self.assertNotRegex(source, r"\b(?:QTest|QSignalSpy|QAbstractItemModelTester)\b|\bQTEST_|<QtTest|<QTest>|\bQ(?:VERIFY|COMPARE|TRY|FETCH|SKIP|FAIL)\w*")

    def test_target_and_negative_contract_tests_are_wired(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        link = re.search(r"target_link_libraries\(iiPhotoLibraryGalleryModelTests\s+([^)]*)\)", cmake)
        self.assertIsNotNone(link)
        self.assertNotIn("Qt6::Test", link.group(1))
        tests = (ROOT / "tests/flat_model_contract_tests.cpp").read_text()
        for case in ("rejectsIncorrectInsertionCount", "rejectsIncorrectRemovalCount", "rejectsLayoutThatLosesPersistentIdentity"):
            self.assertIn(case, tests)

if __name__ == "__main__":
    unittest.main()
