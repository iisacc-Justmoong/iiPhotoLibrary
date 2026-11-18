from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
CASES = ['finalShutdownDoesNotWaitForABlockedPhotoOperation', 'fixtureRootIsProjectResources', 'defaultRefreshIgnoresUnselectedNativePhotosAndPreservesContainerFiles', 'explicitLibraryImportDoesNotAdoptLaterNativePhotosDuringRefresh', 'permissionGrantedForAnOldContainerDoesNotImportIntoItsReplacement', 'openingAndReadingAnEmptyStoreCreatesNoPhotoDirectories', 'photoDiscoveryAndOriginalPreparationCreateOnlyRequiredParents', 'emptyRefreshAndAutomaticConnectionDoNotPersistPhotoState', 'emptyScaffoldingFromAnOlderSessionDoesNotBecomePersistentPhotoState', 'transferCreatesStagingAndCommitParentsWithoutAnInitialRefresh', 'galleryIndexesOutwardAndReprioritizesWithoutExportingOriginals', 'galleryIndexSurvivesCancellationReopenAndRepairsEditedOrMissingPreviews', 'compactGalleryIsPublishedBeforeReadingIndividualReferences', 'explicitConnectionRequestsPhotoAccessAndPublishesAfterGrant', 'automaticPhotoAccessWaitsForAContainerAndNeverOpensSettings', 'indexedPhotosAppearBeforeASlowOriginalFinishesAndReportRealProgress', 'cancelledPhotoWorkReleasesItsFileLockBeforeBackgroundDrainReturns', 'hashingCancellationStopsBeforePublishingAPartialDigest', 'incrementalPhotoBatchesKeepEveryPreviouslyPublishedRecord', 'portableNamesKeepTheirExtensionAndPlatformLimits', 'nativeOriginalsRemainAliasesAndPreviewsPersist', 'duplicatesPreserveAllNativeResourcesAndShareOriginalBytes', 'containerOwnedCopiesAndReceivedRevisionsKeepTheirOriginals', 'localAdditionStaysInTheContainerAndPreservesTheUsersFile', 'transferChecksHashesAndKeepsCommittedOriginalsInsideTheContainer', 'legacyLayoutKeepsNativeReferencesAndOriginals', 'deniedAndLimitedAccessNeverErasePhotos', 'corruptTransferAndRedirectedCacheAreRejected', 'originalEditUpdatesOneObjectAndTrashIsExplicit', 'unavailableAssetDoesNotBlockOtherPhotos', 'mediaDatabaseReplacementDoesNotBecomeGlobalDeletion', 'anotherTransferKeepsItsOriginalLease', 'expiredViewingCopiesAreRemovedWithoutTouchingNativeOriginals', 'unchangedRefreshDoesNotStartAnotherFileSync', 'partialNativeImportCannotReplaceACompleteAlias', 'remoteResourceVersionIsNotOverwrittenByAnUnchangedNativeCopy', 'completedRequestCacheDoesNotStopLongTransfers', 'galleryOrdersPhotosAndVideosFromOldestToNewest', 'chronologicalGalleryOpensAtNewestAndPreservesBrowsing', 'galleryRendersAndLatePreviewKeepsBrowsingPosition', 'authenticatedExchangeUploadsLocalAndDownloadsOnlySelectedOriginal', 'nativeLibrariesExchangeAliasesPreviewsAndOriginalsOverTls']


class PhotosCTestContract(unittest.TestCase):
    def test_original_cases_and_rows_remain(self):
        source = (ROOT / "tests/tst_photos.cpp").read_text()
        for case in CASES:
            if case == "chronologicalGalleryOpensAtNewestAndPreservesBrowsing":
                for row in ("mobile", "desktop"):
                    self.assertIn(f'runner.run("{case}:{row}"', source)
            else:
                self.assertIn(f'runner.run("{case}"', source)
        self.assertIn("photo_test::CTestRunner runner(argc, argv)", source)
        self.assertIn("photo_test::SignalRecorder progress", source)
        self.assertIn("photo_test::TouchSequence(window, touchDevice)", source)
        for checked in ("publicationValid", "resetValid", "previewsValid", "referenceWritten", "transportValid"):
            self.assertIn(f"CTEST_CHECK({checked})", source)
        self.assertNotRegex(source, r"\bQSignalSpy\b|<QTest>|<QtTest|\bQTest::(?:qExec|qWait|addColumn|newRow|currentDataTag|touchEvent)\b|\bQ(?:VERIFY|COMPARE|TRY|FETCH|SKIP|FAIL)\w*")

    def test_targets_do_not_find_or_link_qttest(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertNotRegex(cmake, r"find_package\(Qt6[^)]*\bTest\b")
        for target in ("iiPhotoLibraryTests", "iiPhotoLibraryLegacyViewTests"):
            link = re.search(rf"target_link_libraries\({target}\s+([^)]*)\)", cmake)
            self.assertIsNotNone(link)
            self.assertNotIn("Qt6::Test", link.group(1))
        self.assertIn("iiPhotoLibrary.PhotosCTestContract", cmake)
        self.assertIn("iiPhotoLibrary.TouchSupport", cmake)


if __name__ == "__main__":
    unittest.main()
