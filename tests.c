/*
 * Development tests for Homework #2 - Multi-Level Indexing.
 *
 * This file is NOT submitted. It includes the submission source so it can
 * exercise every internal helper, while a dedicated #define BUILDING_TESTS
 * suppresses the submission's main() so this file's main() runs instead.
 *
 * Tests are grouped by phase:
 *   Phase 1 - JSON loader
 *   Phase 2 - Replacement Selection Sort + Country Index
 *   Phase 3 - City Index
 *   Phase 4 - Product Index + products.dat
 *   Phase 5 - Index-only search
 *   Phase 6 - Insert product
 */

#include "2020510001.c"
#include <unistd.h>

/* qsort comparator over (char *) used only by the tests as a sorting oracle. */
static int compareStringsLexicographically(const void *leftPointer, const void *rightPointer) {
    const char *leftString  = *(const char *const *)leftPointer;
    const char *rightString = *(const char *const *)rightPointer;
    return strcmp(leftString, rightString);
}

/* =========================================================================
 * Tiny test framework
 * ====================================================================== */

static int totalTestsRun       = 0;
static int totalTestsFailed    = 0;
static int currentTestFailures = 0;

#define BEGIN_TEST(name)                                                       \
    do {                                                                       \
        totalTestsRun       += 1;                                              \
        currentTestFailures  = 0;                                              \
        printf("  - %-70s ", name);                                            \
        fflush(stdout);                                                        \
    } while (0)

#define END_TEST()                                                             \
    do {                                                                      \
        if (currentTestFailures == 0) {                                        \
            printf("PASS\n");                                                  \
        } else {                                                              \
            printf("FAIL (%d assertion(s) failed)\n", currentTestFailures);    \
            totalTestsFailed += 1;                                             \
        }                                                                      \
    } while (0)

#define EXPECT_TRUE(condition, failureMessage)                                 \
    do {                                                                      \
        if (!(condition)) {                                                    \
            currentTestFailures += 1;                                          \
            printf("\n      assertion failed: %s", failureMessage);            \
        }                                                                     \
    } while (0)

#define EXPECT_EQUAL_INT(actual, expected, failureMessage)                     \
    do {                                                                      \
        long long actualValue   = (long long)(actual);                         \
        long long expectedValue = (long long)(expected);                       \
        if (actualValue != expectedValue) {                                    \
            currentTestFailures += 1;                                          \
            printf("\n      assertion failed: %s (actual=%lld expected=%lld)", \
                   failureMessage, actualValue, expectedValue);                \
        }                                                                     \
    } while (0)

#define EXPECT_EQUAL_STRING(actual, expected, failureMessage)                  \
    do {                                                                      \
        const char *actualText   = (actual);                                   \
        const char *expectedText = (expected);                                 \
        if (strcmp(actualText, expectedText) != 0) {                           \
            currentTestFailures += 1;                                          \
            printf("\n      assertion failed: %s (actual='%s' expected='%s')", \
                   failureMessage, actualText, expectedText);                  \
        }                                                                     \
    } while (0)

/* =========================================================================
 * Phase 1 - JSON loader tests
 * ====================================================================== */

static void testLoaderProducesEightHundredRecords(void) {
    BEGIN_TEST("Phase 1: loader produces 800 records");
    ProductRecordList list;
    productRecordListInitialize(&list);
    bool ok = loadProductRecordsFromJsonFile("Assignment -2.json", &list);
    EXPECT_TRUE(ok, "loader returned false");
    EXPECT_EQUAL_INT(list.count, 800, "record count must be 800");
    productRecordListRelease(&list);
    END_TEST();
}

static void testLoaderPreservesUtf8Country(void) {
    BEGIN_TEST("Phase 1: loader preserves UTF-8 'Türkiye' byte sequence");
    ProductRecordList list;
    productRecordListInitialize(&list);
    loadProductRecordsFromJsonFile("Assignment -2.json", &list);
    bool foundTurkiye = false;
    for (size_t recordIndex = 0; recordIndex < list.count; recordIndex++) {
        if (strcmp(list.items[recordIndex].countryName, "Türkiye") == 0) {
            foundTurkiye = true;
            break;
        }
    }
    EXPECT_TRUE(foundTurkiye, "no record has countryName == 'Türkiye'");
    productRecordListRelease(&list);
    END_TEST();
}

static void testLoaderHandlesNullIsbnAndWarehouse(void) {
    BEGIN_TEST("Phase 1: loader maps JSON nulls to empty strings");
    ProductRecordList list;
    productRecordListInitialize(&list);
    loadProductRecordsFromJsonFile("Assignment -2.json", &list);
    bool foundEmptyIsbn      = false;
    bool foundEmptyWarehouse = false;
    for (size_t recordIndex = 0; recordIndex < list.count; recordIndex++) {
        if (list.items[recordIndex].isbnText[0]      == '\0') foundEmptyIsbn      = true;
        if (list.items[recordIndex].warehouseCode[0] == '\0') foundEmptyWarehouse = true;
    }
    EXPECT_TRUE(foundEmptyIsbn,      "expected at least one record with empty isbnText");
    EXPECT_TRUE(foundEmptyWarehouse, "expected at least one record with empty warehouseCode");
    productRecordListRelease(&list);
    END_TEST();
}

static void testLoaderFillsAllRequiredFields(void) {
    BEGIN_TEST("Phase 1: every record has product_id, country, city, name");
    ProductRecordList list;
    productRecordListInitialize(&list);
    loadProductRecordsFromJsonFile("Assignment -2.json", &list);
    bool allWellFormed = true;
    for (size_t recordIndex = 0; recordIndex < list.count; recordIndex++) {
        const ProductRecord *record = &list.items[recordIndex];
        if (record->productIdentifier[0] == '\0' ||
            record->countryName[0]       == '\0' ||
            record->cityName[0]          == '\0' ||
            record->productName[0]       == '\0') {
            allWellFormed = false;
            break;
        }
    }
    EXPECT_TRUE(allWellFormed, "at least one record was missing a required field");
    productRecordListRelease(&list);
    END_TEST();
}

/* =========================================================================
 * Phase 2 - Replacement Selection Sort and Country Index tests
 * ====================================================================== */

static void testRssMatchesQsortOnRandomStrings(void) {
    BEGIN_TEST("Phase 2: RSS + merge agrees with qsort on random strings");
    enum { RANDOM_STRING_COUNT = 200, RANDOM_STRING_LENGTH = 8 };
    char  **inputStrings           = calloc(RANDOM_STRING_COUNT, sizeof(char *));
    char  **inputStringsCopyForRss = calloc(RANDOM_STRING_COUNT, sizeof(char *));
    char  **expectedSorted         = calloc(RANDOM_STRING_COUNT, sizeof(char *));
    srand(0xC0FFEE);
    for (size_t stringIndex = 0; stringIndex < RANDOM_STRING_COUNT; stringIndex++) {
        char buffer[RANDOM_STRING_LENGTH + 1];
        for (size_t letterIndex = 0; letterIndex < RANDOM_STRING_LENGTH; letterIndex++) {
            buffer[letterIndex] = 'a' + (rand() % 26);
        }
        buffer[RANDOM_STRING_LENGTH] = '\0';
        inputStrings[stringIndex]           = strdup(buffer);
        inputStringsCopyForRss[stringIndex] = inputStrings[stringIndex];
        expectedSorted[stringIndex]         = inputStrings[stringIndex];
    }
    qsort(expectedSorted, RANDOM_STRING_COUNT, sizeof(char *),
          compareStringsLexicographically);

    StringRunCollection runs;
    replacementSelectionSortStrings(inputStringsCopyForRss, RANDOM_STRING_COUNT,
                                    REPLACEMENT_SELECTION_HEAP_SIZE, false, &runs);
    char **rssSorted = NULL;
    size_t rssCount  = 0;
    mergeStringRunsIntoSortedArray(&runs, &rssSorted, &rssCount);

    EXPECT_EQUAL_INT(rssCount, RANDOM_STRING_COUNT, "merged length must match input length");
    bool allEqual = true;
    for (size_t cmpIndex = 0; cmpIndex < RANDOM_STRING_COUNT && cmpIndex < rssCount; cmpIndex++) {
        if (strcmp(rssSorted[cmpIndex], expectedSorted[cmpIndex]) != 0) {
            allEqual = false;
            break;
        }
    }
    EXPECT_TRUE(allEqual, "RSS+merge output differs from qsort output");

    free(rssSorted);
    releaseStringRunCollection(&runs, false);
    free(inputStringsCopyForRss);
    free(expectedSorted);
    for (size_t freeIndex = 0; freeIndex < RANDOM_STRING_COUNT; freeIndex++) {
        free(inputStrings[freeIndex]);
    }
    free(inputStrings);
    END_TEST();
}

static void testRssProducesMultipleRunsOnDescendingInput(void) {
    BEGIN_TEST("Phase 2: descending input produces multiple runs");
    const size_t descendingCount = 12;
    char *descendingStrings[12]; /* "l", "k", ... "a" */
    for (size_t descIndex = 0; descIndex < descendingCount; descIndex++) {
        char buffer[2] = { (char)('l' - (int)descIndex), '\0' };
        descendingStrings[descIndex] = strdup(buffer);
    }
    StringRunCollection runs;
    replacementSelectionSortStrings(descendingStrings, descendingCount, 4, false, &runs);
    EXPECT_TRUE(runs.runCount >= 3, "descending input with M=4 must produce >= 3 runs");

    /* Confirm each individual run is sorted. */
    bool allRunsSorted = true;
    for (size_t runIndex = 0; runIndex < runs.runCount; runIndex++) {
        for (size_t itemIndex = 1; itemIndex < runs.runs[runIndex].count; itemIndex++) {
            if (strcmp(runs.runs[runIndex].strings[itemIndex - 1],
                       runs.runs[runIndex].strings[itemIndex]) > 0) {
                allRunsSorted = false;
                break;
            }
        }
    }
    EXPECT_TRUE(allRunsSorted, "every produced run must be internally sorted");

    releaseStringRunCollection(&runs, false);
    for (size_t freeIndex = 0; freeIndex < descendingCount; freeIndex++) {
        free(descendingStrings[freeIndex]);
    }
    END_TEST();
}

static void testCountryIndexFileHasTenSortedEntries(void) {
    BEGIN_TEST("Phase 2: country_index.dat holds 10 alphabetically sorted countries");
    ProductRecordList list;
    productRecordListInitialize(&list);
    loadProductRecordsFromJsonFile("Assignment -2.json", &list);
    buildAndWriteCountryIndexFile(&list, COUNTRY_INDEX_FILE_PATH);
    productRecordListRelease(&list);

    FILE *countryIndexFile = fopen(COUNTRY_INDEX_FILE_PATH, "rb");
    EXPECT_TRUE(countryIndexFile != NULL, "country index file must be openable");

    CountryIndexEntry entry;
    char previousName[MAX_COUNTRY_NAME_LENGTH] = "";
    int  entriesRead = 0;
    bool ascending   = true;
    while (fread(&entry, sizeof(entry), 1, countryIndexFile) == 1) {
        if (entriesRead > 0 && strcmp(previousName, entry.countryName) > 0) {
            ascending = false;
        }
        copyStringIntoFixedBuffer(previousName, sizeof(previousName), entry.countryName);
        entriesRead += 1;
    }
    fclose(countryIndexFile);

    EXPECT_EQUAL_INT(entriesRead, 10, "exactly 10 country entries expected");
    EXPECT_TRUE(ascending, "country names must be in ascending order");
    END_TEST();
}

/* =========================================================================
 * Phase 3 - City Index tests
 * ====================================================================== */

/* Helper: read all entries of a file into a freshly allocated array. */
static void *readEntireFileIntoArray(const char *filePath, size_t entrySize, size_t *outCount) {
    FILE *file = fopen(filePath, "rb");
    if (file == NULL) {
        *outCount = 0;
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    *outCount = (size_t)fileSize / entrySize;
    void *buffer = calloc(*outCount, entrySize);
    if (buffer != NULL) {
        if (fread(buffer, entrySize, *outCount, file) != *outCount) {
            free(buffer);
            buffer = NULL;
            *outCount = 0;
        }
    }
    fclose(file);
    return buffer;
}

static void rebuildCountryAndCityIndexes(void) {
    ProductRecordList list;
    productRecordListInitialize(&list);
    loadProductRecordsFromJsonFile("Assignment -2.json", &list);
    buildAndWriteCountryIndexFile(&list, COUNTRY_INDEX_FILE_PATH);
    buildAndWriteCityIndexFile(&list, COUNTRY_INDEX_FILE_PATH, CITY_INDEX_FILE_PATH);
    productRecordListRelease(&list);
}

static void testCityIndexHasEightyEntries(void) {
    BEGIN_TEST("Phase 3: city_index.dat has 80 entries (10 countries x 8 cities)");
    rebuildCountryAndCityIndexes();
    size_t cityCount = 0;
    CityIndexEntry *cityEntries =
        readEntireFileIntoArray(CITY_INDEX_FILE_PATH, sizeof(CityIndexEntry), &cityCount);
    EXPECT_EQUAL_INT(cityCount, 80, "expected 80 city rows");
    free(cityEntries);
    END_TEST();
}

static void testCityChainIsAlphabeticalForEveryCountry(void) {
    BEGIN_TEST("Phase 3: traversing each country's city chain yields alphabetical order");
    rebuildCountryAndCityIndexes();
    size_t cityCount = 0, countryCount = 0;
    CityIndexEntry    *cityEntries =
        readEntireFileIntoArray(CITY_INDEX_FILE_PATH, sizeof(CityIndexEntry), &cityCount);
    CountryIndexEntry *countryEntries =
        readEntireFileIntoArray(COUNTRY_INDEX_FILE_PATH, sizeof(CountryIndexEntry), &countryCount);

    bool allChainsAlphabetical = true;
    int  countriesWithMismatch = 0;
    for (size_t countryIndex = 0; countryIndex < countryCount; countryIndex++) {
        long currentRow = countryEntries[countryIndex].firstCityRowOffset;
        char previousName[MAX_CITY_NAME_LENGTH] = "";
        bool firstStep = true;
        while (currentRow != END_OF_LIST_SENTINEL) {
            const CityIndexEntry *cityEntry = &cityEntries[currentRow - 1];
            if (!firstStep && strcmp(previousName, cityEntry->cityName) > 0) {
                allChainsAlphabetical = false;
                countriesWithMismatch += 1;
                break;
            }
            copyStringIntoFixedBuffer(previousName, sizeof(previousName), cityEntry->cityName);
            firstStep   = false;
            currentRow  = cityEntry->nextCityRowOffset;
        }
    }
    EXPECT_TRUE(allChainsAlphabetical, "at least one country's chain is not alphabetical");
    EXPECT_EQUAL_INT(countriesWithMismatch, 0, "countries with mismatched chain");

    free(cityEntries);
    free(countryEntries);
    END_TEST();
}

static void testCityChainTerminatesWithSentinel(void) {
    BEGIN_TEST("Phase 3: every country's chain ends with END_OF_LIST_SENTINEL");
    rebuildCountryAndCityIndexes();
    size_t cityCount = 0, countryCount = 0;
    CityIndexEntry    *cityEntries =
        readEntireFileIntoArray(CITY_INDEX_FILE_PATH, sizeof(CityIndexEntry), &cityCount);
    CountryIndexEntry *countryEntries =
        readEntireFileIntoArray(COUNTRY_INDEX_FILE_PATH, sizeof(CountryIndexEntry), &countryCount);

    int chainsThatEndCorrectly = 0;
    for (size_t countryIndex = 0; countryIndex < countryCount; countryIndex++) {
        long currentRow      = countryEntries[countryIndex].firstCityRowOffset;
        long lastVisitedRow  = END_OF_LIST_SENTINEL;
        size_t maxStepsAllowed = cityCount + 1;
        while (currentRow != END_OF_LIST_SENTINEL && maxStepsAllowed-- > 0) {
            lastVisitedRow = currentRow;
            currentRow     = cityEntries[currentRow - 1].nextCityRowOffset;
        }
        if (lastVisitedRow != END_OF_LIST_SENTINEL &&
            cityEntries[lastVisitedRow - 1].nextCityRowOffset == END_OF_LIST_SENTINEL) {
            chainsThatEndCorrectly += 1;
        }
    }
    EXPECT_EQUAL_INT(chainsThatEndCorrectly, 10, "all 10 country chains must end in -1");

    free(cityEntries);
    free(countryEntries);
    END_TEST();
}

static void testPhysicalCityOrderIsNotAlphabeticalForAtLeastOneCountry(void) {
    BEGIN_TEST("Phase 3: physical row order differs from alphabetical for >= 1 country");
    rebuildCountryAndCityIndexes();
    size_t cityCount = 0;
    CityIndexEntry *cityEntries =
        readEntireFileIntoArray(CITY_INDEX_FILE_PATH, sizeof(CityIndexEntry), &cityCount);

    bool foundCountryWithDisorder = false;
    for (size_t startIndex = 0; startIndex + 1 < cityCount; startIndex++) {
        if (strcmp(cityEntries[startIndex].owningCountryName,
                   cityEntries[startIndex + 1].owningCountryName) != 0) {
            continue;
        }
        if (strcmp(cityEntries[startIndex].cityName,
                   cityEntries[startIndex + 1].cityName) > 0) {
            foundCountryWithDisorder = true;
            break;
        }
    }
    EXPECT_TRUE(foundCountryWithDisorder,
                "expected at least one country to have non-alphabetical physical order");

    free(cityEntries);
    END_TEST();
}

/* =========================================================================
 * Phase 4 - Product Index + products.dat tests
 * ====================================================================== */

static void rebuildEverythingThroughPhaseFour(void) {
    ProductRecordList list;
    productRecordListInitialize(&list);
    loadProductRecordsFromJsonFile("Assignment -2.json", &list);
    buildAndWriteCountryIndexFile(&list, COUNTRY_INDEX_FILE_PATH);
    buildAndWriteCityIndexFile(&list, COUNTRY_INDEX_FILE_PATH, CITY_INDEX_FILE_PATH);
    buildAndWriteProductIndexAndDataFile(&list, CITY_INDEX_FILE_PATH,
                                         PRODUCT_INDEX_FILE_PATH, DATA_FILE_PATH);
    productRecordListRelease(&list);
}

static void testProductIndexHasEightHundredEntries(void) {
    BEGIN_TEST("Phase 4: product_index.dat has 800 entries");
    rebuildEverythingThroughPhaseFour();
    size_t productCount = 0;
    ProductIndexEntry *productEntries =
        readEntireFileIntoArray(PRODUCT_INDEX_FILE_PATH, sizeof(ProductIndexEntry), &productCount);
    EXPECT_EQUAL_INT(productCount, 800, "expected 800 product index entries");
    free(productEntries);
    END_TEST();
}

static void testDataFileHasEightHundredFixedSizeRecords(void) {
    BEGIN_TEST("Phase 4: products.dat is exactly 800 * sizeof(ProductRecord) bytes");
    rebuildEverythingThroughPhaseFour();
    FILE *dataFile = fopen(DATA_FILE_PATH, "rb");
    EXPECT_TRUE(dataFile != NULL, "products.dat must exist");
    fseek(dataFile, 0, SEEK_END);
    long fileSize = ftell(dataFile);
    fclose(dataFile);
    EXPECT_EQUAL_INT(fileSize, 800 * (long)sizeof(ProductRecord),
                     "products.dat size mismatch");
    END_TEST();
}

static void testProductChainIsAlphabeticalForEveryCity(void) {
    BEGIN_TEST("Phase 4: each city's product chain is in alphabetical order");
    rebuildEverythingThroughPhaseFour();
    size_t cityCount = 0, productCount = 0;
    CityIndexEntry    *cityEntries =
        readEntireFileIntoArray(CITY_INDEX_FILE_PATH, sizeof(CityIndexEntry), &cityCount);
    ProductIndexEntry *productEntries =
        readEntireFileIntoArray(PRODUCT_INDEX_FILE_PATH, sizeof(ProductIndexEntry), &productCount);

    bool allChainsAlphabetical = true;
    for (size_t cityIndex = 0; cityIndex < cityCount; cityIndex++) {
        long currentRow = cityEntries[cityIndex].firstProductRowOffset;
        char previousProductName[MAX_PRODUCT_NAME_LENGTH] = "";
        bool firstStep = true;
        while (currentRow != END_OF_LIST_SENTINEL) {
            const ProductIndexEntry *productEntry = &productEntries[currentRow - 1];
            if (!firstStep && strcmp(previousProductName, productEntry->productName) > 0) {
                allChainsAlphabetical = false;
                break;
            }
            copyStringIntoFixedBuffer(previousProductName, sizeof(previousProductName),
                                      productEntry->productName);
            firstStep   = false;
            currentRow  = productEntry->nextProductRowOffset;
        }
        if (!allChainsAlphabetical) break;
    }
    EXPECT_TRUE(allChainsAlphabetical, "found a city whose product chain is out of order");

    free(cityEntries);
    free(productEntries);
    END_TEST();
}

static void testProductIndexPointsToCorrectDataRecord(void) {
    BEGIN_TEST("Phase 4: each product index entry points at the matching .dat record");
    rebuildEverythingThroughPhaseFour();
    size_t productCount = 0;
    ProductIndexEntry *productEntries =
        readEntireFileIntoArray(PRODUCT_INDEX_FILE_PATH, sizeof(ProductIndexEntry), &productCount);
    FILE *dataFile = fopen(DATA_FILE_PATH, "rb");
    EXPECT_TRUE(dataFile != NULL, "products.dat must be openable");

    bool allMatch = true;
    for (size_t productIndex = 0; productIndex < productCount; productIndex++) {
        ProductRecord recordOnDisk;
        fseek(dataFile, productEntries[productIndex].dataFileRecordOffset, SEEK_SET);
        if (fread(&recordOnDisk, sizeof(recordOnDisk), 1, dataFile) != 1) {
            allMatch = false;
            break;
        }
        if (strcmp(recordOnDisk.productName,
                   productEntries[productIndex].productName) != 0 ||
            strcmp(recordOnDisk.cityName,
                   productEntries[productIndex].owningCityName) != 0) {
            allMatch = false;
            break;
        }
    }
    EXPECT_TRUE(allMatch, "at least one product index entry points to the wrong record");

    fclose(dataFile);
    free(productEntries);
    END_TEST();
}

static void testEveryDataRecordIsReachableExactlyOnceFromIndexRoot(void) {
    BEGIN_TEST("Phase 4: full traversal reaches every record exactly once");
    rebuildEverythingThroughPhaseFour();
    size_t countryCount = 0, cityCount = 0, productCount = 0;
    CountryIndexEntry *countryEntries =
        readEntireFileIntoArray(COUNTRY_INDEX_FILE_PATH, sizeof(CountryIndexEntry), &countryCount);
    CityIndexEntry    *cityEntries    =
        readEntireFileIntoArray(CITY_INDEX_FILE_PATH, sizeof(CityIndexEntry), &cityCount);
    ProductIndexEntry *productEntries =
        readEntireFileIntoArray(PRODUCT_INDEX_FILE_PATH, sizeof(ProductIndexEntry), &productCount);

    int *visitCount = calloc(productCount, sizeof(int));
    for (size_t countryIndex = 0; countryIndex < countryCount; countryIndex++) {
        long cityRow = countryEntries[countryIndex].firstCityRowOffset;
        while (cityRow != END_OF_LIST_SENTINEL) {
            long productRow = cityEntries[cityRow - 1].firstProductRowOffset;
            while (productRow != END_OF_LIST_SENTINEL) {
                visitCount[productRow - 1] += 1;
                productRow = productEntries[productRow - 1].nextProductRowOffset;
            }
            cityRow = cityEntries[cityRow - 1].nextCityRowOffset;
        }
    }
    int notVisitedExactlyOnce = 0;
    for (size_t productIndex = 0; productIndex < productCount; productIndex++) {
        if (visitCount[productIndex] != 1) notVisitedExactlyOnce += 1;
    }
    EXPECT_EQUAL_INT(notVisitedExactlyOnce, 0,
                     "every product row should be visited exactly once");

    free(visitCount);
    free(productEntries);
    free(cityEntries);
    free(countryEntries);
    END_TEST();
}

/* =========================================================================
 * Phase 5 - Index-only search tests
 * ====================================================================== */

static int redirectStdoutToTempFile(int *outOriginalStdoutFd) {
    fflush(stdout);
    *outOriginalStdoutFd = dup(fileno(stdout));
    FILE *temporary = tmpfile();
    dup2(fileno(temporary), fileno(stdout));
    return fileno(temporary);
}

static void restoreStdout(int originalStdoutFd) {
    fflush(stdout);
    dup2(originalStdoutFd, fileno(stdout));
    close(originalStdoutFd);
}

static void testSearchByCountryReturnsEightyProducts(void) {
    BEGIN_TEST("Phase 5: search by country 'Germany' returns 80 products");
    rebuildEverythingThroughPhaseFour();
    IndexAccessSession session;
    EXPECT_TRUE(openIndexAccessSession(&session, COUNTRY_INDEX_FILE_PATH,
                                       CITY_INDEX_FILE_PATH, PRODUCT_INDEX_FILE_PATH,
                                       DATA_FILE_PATH),
                "session must open all four files");
    int originalStdoutFd = -1;
    redirectStdoutToTempFile(&originalStdoutFd);
    size_t found = searchAndPrintByCountry(&session, "Germany", false);
    restoreStdout(originalStdoutFd);
    EXPECT_EQUAL_INT(found, 80, "Germany should have 8 cities x 10 products = 80");
    closeIndexAccessSession(&session);
    END_TEST();
}

static void testSearchByCityReturnsTenProducts(void) {
    BEGIN_TEST("Phase 5: search by city 'Berlin' returns 10 products");
    rebuildEverythingThroughPhaseFour();
    IndexAccessSession session;
    openIndexAccessSession(&session, COUNTRY_INDEX_FILE_PATH, CITY_INDEX_FILE_PATH,
                           PRODUCT_INDEX_FILE_PATH, DATA_FILE_PATH);
    int originalStdoutFd = -1;
    redirectStdoutToTempFile(&originalStdoutFd);
    size_t found = searchAndPrintByCity(&session, "Berlin", false);
    restoreStdout(originalStdoutFd);
    EXPECT_EQUAL_INT(found, 10, "Berlin must have 10 products");
    closeIndexAccessSession(&session);
    END_TEST();
}

static void testSearchByProductNameReadsDataFileOnlyForMatches(void) {
    BEGIN_TEST("Phase 5: searching by product reads .dat exactly N times for N matches");
    rebuildEverythingThroughPhaseFour();
    IndexAccessSession session;
    openIndexAccessSession(&session, COUNTRY_INDEX_FILE_PATH, CITY_INDEX_FILE_PATH,
                           PRODUCT_INDEX_FILE_PATH, DATA_FILE_PATH);

    /* Read the first product record's name from the .dat file directly. */
    ProductRecord firstRecord;
    fseek(session.dataFile, 0, SEEK_SET);
    fread(&firstRecord, sizeof(firstRecord), 1, session.dataFile);
    char targetProductName[MAX_PRODUCT_NAME_LENGTH];
    copyStringIntoFixedBuffer(targetProductName, sizeof(targetProductName),
                              firstRecord.productName);

    session.dataFileReadCount = 0;
    int originalStdoutFd = -1;
    redirectStdoutToTempFile(&originalStdoutFd);
    size_t matches = searchAndPrintByProductName(&session, targetProductName, false);
    restoreStdout(originalStdoutFd);

    EXPECT_TRUE(matches >= 1, "expected at least one match for an existing name");
    EXPECT_EQUAL_INT(session.dataFileReadCount, (long)matches,
                     ".dat reads must equal the number of matches (no full scan)");
    closeIndexAccessSession(&session);
    END_TEST();
}

static void testSearchByMissingCountryReturnsZeroAndDoesNotReadDataFile(void) {
    BEGIN_TEST("Phase 5: search for unknown country reads zero data records");
    rebuildEverythingThroughPhaseFour();
    IndexAccessSession session;
    openIndexAccessSession(&session, COUNTRY_INDEX_FILE_PATH, CITY_INDEX_FILE_PATH,
                           PRODUCT_INDEX_FILE_PATH, DATA_FILE_PATH);
    session.dataFileReadCount = 0;
    int originalStdoutFd = -1;
    redirectStdoutToTempFile(&originalStdoutFd);
    size_t found = searchAndPrintByCountry(&session, "Atlantis", false);
    restoreStdout(originalStdoutFd);
    EXPECT_EQUAL_INT(found, 0, "no products expected for unknown country");
    EXPECT_EQUAL_INT(session.dataFileReadCount, 0, "no .dat reads expected");
    closeIndexAccessSession(&session);
    END_TEST();
}

/* =========================================================================
 * Phase 6 - Insert product tests
 * ====================================================================== */

/* Build a fresh ProductRecord with sensible defaults except the names. */
static ProductRecord makeProductRecordForTest(const char *productName,
                                              const char *cityName,
                                              const char *countryName) {
    ProductRecord record;
    memset(&record, 0, sizeof(record));
    copyStringIntoFixedBuffer(record.productIdentifier, sizeof(record.productIdentifier),
                              "TEST-XXXXXX");
    copyStringIntoFixedBuffer(record.countryName,    sizeof(record.countryName),    countryName);
    copyStringIntoFixedBuffer(record.cityName,       sizeof(record.cityName),       cityName);
    copyStringIntoFixedBuffer(record.productName,    sizeof(record.productName),    productName);
    copyStringIntoFixedBuffer(record.productBrand,   sizeof(record.productBrand),   "TestBrand");
    copyStringIntoFixedBuffer(record.productCategory, sizeof(record.productCategory), "TestCat");
    record.priceAmount    = 99;
    copyStringIntoFixedBuffer(record.priceCurrency,   sizeof(record.priceCurrency),   "EUR");
    record.inventoryStock = 1;
    return record;
}

static void testInsertAtAlphabeticalHeadOfBerlin(void) {
    BEGIN_TEST("Phase 6: insert at alphabetical head splices through firstProductRowOffset");
    rebuildEverythingThroughPhaseFour();
    IndexAccessSession session;
    openIndexAccessSession(&session, COUNTRY_INDEX_FILE_PATH, CITY_INDEX_FILE_PATH,
                           PRODUCT_INDEX_FILE_PATH, DATA_FILE_PATH);
    long cityRow = findCityRowByCityAndCountry(&session, "Berlin", "Germany");
    CityIndexEntry beforeEntry;
    readCityEntryAtRow(&session, cityRow, &beforeEntry);
    long oldHead = beforeEntry.firstProductRowOffset;

    ProductRecord newRecord = makeProductRecordForTest("Aardvark Workstation",
                                                       "Berlin", "Germany");
    InsertProductOutcome outcome = insertProductIntoCity(&session, &newRecord, false);
    EXPECT_EQUAL_INT(outcome, INSERT_OK, "insert should succeed");

    CityIndexEntry afterEntry;
    readCityEntryAtRow(&session, cityRow, &afterEntry);
    EXPECT_TRUE(afterEntry.firstProductRowOffset != oldHead,
                "city's firstProductRowOffset should now point to the new row");

    ProductIndexEntry newHeadEntry;
    readProductEntryAtRow(&session, afterEntry.firstProductRowOffset, &newHeadEntry);
    EXPECT_EQUAL_STRING(newHeadEntry.productName, "Aardvark Workstation",
                        "new head row must hold the inserted product");
    EXPECT_EQUAL_INT(newHeadEntry.nextProductRowOffset, oldHead,
                     "new row's next must be the old head row");

    closeIndexAccessSession(&session);
    END_TEST();
}

static void testInsertInMiddleKeepsChainAlphabetical(void) {
    BEGIN_TEST("Phase 6: insert in middle splices and keeps chain alphabetical");
    rebuildEverythingThroughPhaseFour();
    IndexAccessSession session;
    openIndexAccessSession(&session, COUNTRY_INDEX_FILE_PATH, CITY_INDEX_FILE_PATH,
                           PRODUCT_INDEX_FILE_PATH, DATA_FILE_PATH);

    /* Use a name that lexically lands inside Berlin's existing chain. */
    ProductRecord newRecord = makeProductRecordForTest("Mango MagicBlender",
                                                       "Berlin", "Germany");
    InsertProductOutcome outcome = insertProductIntoCity(&session, &newRecord, false);
    EXPECT_EQUAL_INT(outcome, INSERT_OK, "insert should succeed");

    /* Walk the chain and verify it is still alphabetical and contains the new name. */
    long cityRow = findCityRowByCityAndCountry(&session, "Berlin", "Germany");
    CityIndexEntry cityEntry;
    readCityEntryAtRow(&session, cityRow, &cityEntry);
    long currentRow = cityEntry.firstProductRowOffset;
    char previousProductName[MAX_PRODUCT_NAME_LENGTH] = "";
    bool ascending = true;
    bool foundNew  = false;
    bool firstStep = true;
    while (currentRow != END_OF_LIST_SENTINEL) {
        ProductIndexEntry productEntry;
        readProductEntryAtRow(&session, currentRow, &productEntry);
        if (!firstStep && strcmp(previousProductName, productEntry.productName) > 0) {
            ascending = false;
            break;
        }
        if (strcmp(productEntry.productName, "Mango MagicBlender") == 0) foundNew = true;
        copyStringIntoFixedBuffer(previousProductName, sizeof(previousProductName),
                                  productEntry.productName);
        firstStep   = false;
        currentRow  = productEntry.nextProductRowOffset;
    }
    EXPECT_TRUE(ascending, "chain should still be alphabetical after middle insert");
    EXPECT_TRUE(foundNew,  "new product should appear in the chain");

    closeIndexAccessSession(&session);
    END_TEST();
}

static void testInsertAtAlphabeticalTailGetsMinusOneSentinel(void) {
    BEGIN_TEST("Phase 6: insert past the tail leaves END_OF_LIST_SENTINEL on the new row");
    rebuildEverythingThroughPhaseFour();
    IndexAccessSession session;
    openIndexAccessSession(&session, COUNTRY_INDEX_FILE_PATH, CITY_INDEX_FILE_PATH,
                           PRODUCT_INDEX_FILE_PATH, DATA_FILE_PATH);

    ProductRecord newRecord = makeProductRecordForTest("zzz Truly Final Gadget",
                                                       "Berlin", "Germany");
    InsertProductOutcome outcome = insertProductIntoCity(&session, &newRecord, false);
    EXPECT_EQUAL_INT(outcome, INSERT_OK, "insert should succeed");

    /* Walk to the tail and confirm both pointer rules. */
    long cityRow = findCityRowByCityAndCountry(&session, "Berlin", "Germany");
    CityIndexEntry cityEntry;
    readCityEntryAtRow(&session, cityRow, &cityEntry);
    long currentRow      = cityEntry.firstProductRowOffset;
    long previousRow     = END_OF_LIST_SENTINEL;
    long lastVisitedRow  = END_OF_LIST_SENTINEL;
    while (currentRow != END_OF_LIST_SENTINEL) {
        ProductIndexEntry productEntry;
        readProductEntryAtRow(&session, currentRow, &productEntry);
        previousRow     = lastVisitedRow;
        lastVisitedRow  = currentRow;
        currentRow      = productEntry.nextProductRowOffset;
    }
    EXPECT_TRUE(lastVisitedRow != END_OF_LIST_SENTINEL, "tail row must exist");
    ProductIndexEntry tailEntry;
    readProductEntryAtRow(&session, lastVisitedRow, &tailEntry);
    EXPECT_EQUAL_STRING(tailEntry.productName, "zzz Truly Final Gadget",
                        "tail must be the inserted product");
    EXPECT_EQUAL_INT(tailEntry.nextProductRowOffset, END_OF_LIST_SENTINEL,
                     "tail's next pointer must be -1");
    (void)previousRow;

    closeIndexAccessSession(&session);
    END_TEST();
}

static void testInsertingDuplicateNameInSameCityIsRejected(void) {
    BEGIN_TEST("Phase 6: inserting a duplicate name in the same city is rejected");
    rebuildEverythingThroughPhaseFour();
    IndexAccessSession session;
    openIndexAccessSession(&session, COUNTRY_INDEX_FILE_PATH, CITY_INDEX_FILE_PATH,
                           PRODUCT_INDEX_FILE_PATH, DATA_FILE_PATH);

    /* Read the first product currently in Berlin and try to insert the same name. */
    long cityRow = findCityRowByCityAndCountry(&session, "Berlin", "Germany");
    CityIndexEntry cityEntry;
    readCityEntryAtRow(&session, cityRow, &cityEntry);
    ProductIndexEntry firstEntry;
    readProductEntryAtRow(&session, cityEntry.firstProductRowOffset, &firstEntry);

    ProductRecord duplicateRecord = makeProductRecordForTest(firstEntry.productName,
                                                             "Berlin", "Germany");
    InsertProductOutcome outcome = insertProductIntoCity(&session, &duplicateRecord, false);
    EXPECT_EQUAL_INT(outcome, INSERT_DUPLICATE_NAME_IN_CITY,
                     "duplicate insert should be rejected");
    closeIndexAccessSession(&session);
    END_TEST();
}

/* =========================================================================
 * Test runner
 * ====================================================================== */

int main(void) {
    printf("Running development tests for Homework #2\n");
    printf("Phase 1 - JSON loader:\n");
    testLoaderProducesEightHundredRecords();
    testLoaderPreservesUtf8Country();
    testLoaderHandlesNullIsbnAndWarehouse();
    testLoaderFillsAllRequiredFields();

    printf("Phase 2 - Replacement Selection Sort + Country Index:\n");
    testRssMatchesQsortOnRandomStrings();
    testRssProducesMultipleRunsOnDescendingInput();
    testCountryIndexFileHasTenSortedEntries();

    printf("Phase 3 - City Index:\n");
    testCityIndexHasEightyEntries();
    testCityChainIsAlphabeticalForEveryCountry();
    testCityChainTerminatesWithSentinel();
    testPhysicalCityOrderIsNotAlphabeticalForAtLeastOneCountry();

    printf("Phase 4 - Product Index + products.dat:\n");
    testProductIndexHasEightHundredEntries();
    testDataFileHasEightHundredFixedSizeRecords();
    testProductChainIsAlphabeticalForEveryCity();
    testProductIndexPointsToCorrectDataRecord();
    testEveryDataRecordIsReachableExactlyOnceFromIndexRoot();

    printf("Phase 5 - Index-only search:\n");
    testSearchByCountryReturnsEightyProducts();
    testSearchByCityReturnsTenProducts();
    testSearchByProductNameReadsDataFileOnlyForMatches();
    testSearchByMissingCountryReturnsZeroAndDoesNotReadDataFile();

    printf("Phase 6 - Insert product:\n");
    testInsertAtAlphabeticalHeadOfBerlin();
    testInsertInMiddleKeepsChainAlphabetical();
    testInsertAtAlphabeticalTailGetsMinusOneSentinel();
    testInsertingDuplicateNameInSameCityIsRejected();

    printf("\n%d tests run, %d failed\n", totalTestsRun, totalTestsFailed);
    return (totalTestsFailed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
