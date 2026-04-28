/*
 * Homework #2 - Multi-Level Secondary Indexing System
 * Student ID: 2020510001
 *
 * Builds a three-level index hierarchy (Country -> City -> Product) on top
 * of a binary data file, using Replacement Selection Sort to produce sorted
 * runs and linked-list offset pointers to maintain alphabetical traversal
 * without ever rewriting the underlying data file.
 *
 * Sections:
 *   1. Public types and constants
 *   2. Replacement Selection Sort (with run merge)
 *   3. JSON loader -> in-memory ProductRecord array
 *   4. Binary file writers (.dat + three index files)
 *   5. Index-only traversal and search
 *   6. Insert-product flow with pointer recalculation
 *   7. Interactive console UI
 *   8. Self-test harness (menu option [9])
 */

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json.h>

/* ===========================================================================
 * Section 1 - Public types and constants
 * ======================================================================== */

#define MAX_PRODUCT_IDENTIFIER_LENGTH    24
#define MAX_COUNTRY_NAME_LENGTH          32
#define MAX_CITY_NAME_LENGTH             32
#define MAX_PRODUCT_NAME_LENGTH          64
#define MAX_BRAND_NAME_LENGTH            32
#define MAX_CATEGORY_NAME_LENGTH         32
#define MAX_CURRENCY_CODE_LENGTH          8
#define MAX_WAREHOUSE_CODE_LENGTH         8
#define MAX_ISBN_LENGTH                  24
#define MAX_PRODUCT_DESCRIPTION_LENGTH  128
#define MAX_EXTRA_TAG_LENGTH             16

/* Replacement Selection Sort working-set size (heap capacity).
 * Small on purpose so the algorithm produces multiple runs that
 * must then be merged - this is what the assignment expects to see. */
#define REPLACEMENT_SELECTION_HEAP_SIZE   4

/* Sentinel meaning "no further row" in any linked-list offset column. */
#define END_OF_LIST_SENTINEL              (-1L)

/* Default file paths used by the program. */
#define DEFAULT_JSON_INPUT_PATH           "Assignment -2.json"
#define DATA_FILE_PATH                    "products.dat"
#define COUNTRY_INDEX_FILE_PATH           "country_index.dat"
#define CITY_INDEX_FILE_PATH              "city_index.dat"
#define PRODUCT_INDEX_FILE_PATH           "product_index.dat"

/* In-memory product record loaded from the JSON dataset.
 * All character fields are fixed width and null-terminated so the
 * on-disk size is constant - required for offset-based access. */
typedef struct {
    char productIdentifier   [MAX_PRODUCT_IDENTIFIER_LENGTH];
    char countryName         [MAX_COUNTRY_NAME_LENGTH];
    char cityName            [MAX_CITY_NAME_LENGTH];
    char productName         [MAX_PRODUCT_NAME_LENGTH];
    char productBrand        [MAX_BRAND_NAME_LENGTH];
    char productCategory     [MAX_CATEGORY_NAME_LENGTH];
    int  priceAmount;
    char priceCurrency       [MAX_CURRENCY_CODE_LENGTH];
    int  inventoryStock;
    char warehouseCode       [MAX_WAREHOUSE_CODE_LENGTH];
    char isbnText            [MAX_ISBN_LENGTH];
    char productDescription  [MAX_PRODUCT_DESCRIPTION_LENGTH];
    char extraTag            [MAX_EXTRA_TAG_LENGTH];
} ProductRecord;

/* Country index entry (Level 1, physically sorted in the file). */
typedef struct {
    char countryName[MAX_COUNTRY_NAME_LENGTH];
    long firstCityRowOffset;       /* 1-based row in the city index; -1 if empty */
} CountryIndexEntry;

/* City index entry (Level 2, insertion order, linked alphabetically). */
typedef struct {
    char cityName            [MAX_CITY_NAME_LENGTH];
    char owningCountryName   [MAX_COUNTRY_NAME_LENGTH];
    long nextCityRowOffset;        /* 1-based row of next city alphabetically; -1 if last */
    long firstProductRowOffset;    /* 1-based row in the product index; -1 if empty */
} CityIndexEntry;

/* Product index entry (Level 3). */
typedef struct {
    char productName         [MAX_PRODUCT_NAME_LENGTH];
    char owningCityName      [MAX_CITY_NAME_LENGTH];
    long nextProductRowOffset;     /* 1-based row of next product alphabetically; -1 if last */
    long dataFileRecordOffset;     /* byte offset of the record in products.dat */
} ProductIndexEntry;

/* Dynamic array of ProductRecord values. */
typedef struct {
    ProductRecord *items;
    size_t         count;
    size_t         capacity;
} ProductRecordList;

/* ===========================================================================
 * Forward declarations
 * ======================================================================== */

static void copyStringIntoFixedBuffer(char *destination, size_t destinationCapacity,
                                      const char *source);
static void productRecordListInitialize(ProductRecordList *list);
static void productRecordListAppend(ProductRecordList *list, const ProductRecord *record);
static void productRecordListRelease(ProductRecordList *list);

static bool loadProductRecordsFromJsonFile(const char *jsonFilePath, ProductRecordList *outList);

typedef struct {
    char  **strings;
    size_t  count;
} StringRun;

typedef struct {
    StringRun *runs;
    size_t     runCount;
} StringRunCollection;

static void replacementSelectionSortStrings(char **inputStrings, size_t inputCount,
                                            size_t heapCapacity, bool printTrace,
                                            StringRunCollection *outRuns);
static void mergeStringRunsIntoSortedArray(const StringRunCollection *runs,
                                           char ***outSortedStrings, size_t *outSortedCount);
static void releaseStringRunCollection(StringRunCollection *runs, bool freeStringMemory);
static char **extractDistinctStringsInEncounterOrder(char **strings, size_t count,
                                                     size_t *outDistinctCount);
static void freeStringArray(char **strings, size_t count);

static void buildAndWriteCountryIndexFile(const ProductRecordList *productList,
                                          const char *countryIndexFilePath);
static void buildAndWriteCityIndexFile(const ProductRecordList *productList,
                                       const char *countryIndexFilePath,
                                       const char *cityIndexFilePath);
static void buildAndWriteProductIndexAndDataFile(const ProductRecordList *productList,
                                                 const char *cityIndexFilePath,
                                                 const char *productIndexFilePath,
                                                 const char *dataFilePath);

/* ===========================================================================
 * Section 1 - Small string helpers
 * ======================================================================== */

/* Copy a possibly-NULL source string into a fixed-size destination buffer,
 * guaranteeing null termination and never overflowing. */
static void copyStringIntoFixedBuffer(char *destination, size_t destinationCapacity,
                                      const char *source) {
    if (destinationCapacity == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    size_t sourceLength = strlen(source);
    size_t copyLength   = (sourceLength < destinationCapacity - 1)
                          ? sourceLength
                          : destinationCapacity - 1;
    memcpy(destination, source, copyLength);
    destination[copyLength] = '\0';
}

/* ===========================================================================
 * Section 1 - ProductRecordList (dynamic array)
 * ======================================================================== */

static void productRecordListInitialize(ProductRecordList *list) {
    list->items    = NULL;
    list->count    = 0;
    list->capacity = 0;
}

static void productRecordListAppend(ProductRecordList *list, const ProductRecord *record) {
    if (list->count == list->capacity) {
        size_t newCapacity = (list->capacity == 0) ? 64 : list->capacity * 2;
        ProductRecord *grown = realloc(list->items, newCapacity * sizeof(ProductRecord));
        if (grown == NULL) {
            fprintf(stderr, "Out of memory while growing product list\n");
            exit(EXIT_FAILURE);
        }
        list->items    = grown;
        list->capacity = newCapacity;
    }
    list->items[list->count++] = *record;
}

static void productRecordListRelease(ProductRecordList *list) {
    free(list->items);
    list->items    = NULL;
    list->count    = 0;
    list->capacity = 0;
}

/* ===========================================================================
 * Section 3 - JSON loader
 *
 * The course-provided JsonParse.c demonstrates the json-c API; this loader
 * uses the same idioms (json_object_object_get_ex, json_object_array_*) but
 * walks the specific schema of Assignment -2.json: an array of countries,
 * each with cities[], each city with products[].
 * ======================================================================== */

/* Read a string field from a json object, write into a fixed buffer.
 * Treats missing or null JSON fields as the empty string. */
static void readJsonStringField(struct json_object *parent, const char *fieldName,
                                char *destination, size_t destinationCapacity) {
    struct json_object *fieldValue = NULL;
    if (!json_object_object_get_ex(parent, fieldName, &fieldValue) || fieldValue == NULL) {
        copyStringIntoFixedBuffer(destination, destinationCapacity, NULL);
        return;
    }
    if (json_object_get_type(fieldValue) == json_type_null) {
        copyStringIntoFixedBuffer(destination, destinationCapacity, NULL);
        return;
    }
    copyStringIntoFixedBuffer(destination, destinationCapacity,
                              json_object_get_string(fieldValue));
}

/* Read an integer field; returns 0 if missing or null. */
static int readJsonIntField(struct json_object *parent, const char *fieldName) {
    struct json_object *fieldValue = NULL;
    if (!json_object_object_get_ex(parent, fieldName, &fieldValue) || fieldValue == NULL) {
        return 0;
    }
    if (json_object_get_type(fieldValue) == json_type_null) {
        return 0;
    }
    return json_object_get_int(fieldValue);
}

/* Populate a ProductRecord from a single product json object plus its parent
 * country/city names. Returns true on success. */
static bool populateProductRecordFromJson(struct json_object *productNode,
                                          const char *countryName,
                                          const char *cityName,
                                          ProductRecord *record) {
    memset(record, 0, sizeof(*record));

    copyStringIntoFixedBuffer(record->countryName, sizeof(record->countryName), countryName);
    copyStringIntoFixedBuffer(record->cityName,    sizeof(record->cityName),    cityName);

    readJsonStringField(productNode, "product_id",
                        record->productIdentifier, sizeof(record->productIdentifier));

    struct json_object *productInfoNode = NULL;
    if (json_object_object_get_ex(productNode, "product_info", &productInfoNode)
        && productInfoNode != NULL) {
        readJsonStringField(productInfoNode, "name",
                            record->productName, sizeof(record->productName));
        readJsonStringField(productInfoNode, "brand",
                            record->productBrand, sizeof(record->productBrand));
        readJsonStringField(productInfoNode, "category",
                            record->productCategory, sizeof(record->productCategory));
    }

    struct json_object *pricingNode = NULL;
    if (json_object_object_get_ex(productNode, "pricing", &pricingNode)
        && pricingNode != NULL) {
        record->priceAmount = readJsonIntField(pricingNode, "price");
        readJsonStringField(pricingNode, "currency",
                            record->priceCurrency, sizeof(record->priceCurrency));
    }

    struct json_object *inventoryNode = NULL;
    if (json_object_object_get_ex(productNode, "inventory", &inventoryNode)
        && inventoryNode != NULL) {
        record->inventoryStock = readJsonIntField(inventoryNode, "stock");
        readJsonStringField(inventoryNode, "warehouse",
                            record->warehouseCode, sizeof(record->warehouseCode));
    }

    readJsonStringField(productNode, "isbn",
                        record->isbnText, sizeof(record->isbnText));
    readJsonStringField(productNode, "description",
                        record->productDescription, sizeof(record->productDescription));
    readJsonStringField(productNode, "extra",
                        record->extraTag, sizeof(record->extraTag));

    return record->productIdentifier[0] != '\0';
}

/* Parse the JSON file at the given path and append every product to outList.
 * Returns true on success and false (with stderr message) on any I/O or
 * parse failure. */
static bool loadProductRecordsFromJsonFile(const char *jsonFilePath,
                                           ProductRecordList *outList) {
    struct json_object *rootArray = json_object_from_file(jsonFilePath);
    if (rootArray == NULL) {
        fprintf(stderr, "Failed to load JSON from '%s'\n", jsonFilePath);
        return false;
    }
    if (json_object_get_type(rootArray) != json_type_array) {
        fprintf(stderr, "Top-level JSON value must be an array of countries\n");
        json_object_put(rootArray);
        return false;
    }

    size_t countryCount = json_object_array_length(rootArray);
    for (size_t countryIndex = 0; countryIndex < countryCount; countryIndex++) {
        struct json_object *countryNode = json_object_array_get_idx(rootArray, countryIndex);
        if (countryNode == NULL) continue;

        char countryName[MAX_COUNTRY_NAME_LENGTH];
        readJsonStringField(countryNode, "country", countryName, sizeof(countryName));

        struct json_object *citiesArray = NULL;
        if (!json_object_object_get_ex(countryNode, "cities", &citiesArray)
            || citiesArray == NULL) {
            continue;
        }

        size_t cityCount = json_object_array_length(citiesArray);
        for (size_t cityIndex = 0; cityIndex < cityCount; cityIndex++) {
            struct json_object *cityNode = json_object_array_get_idx(citiesArray, cityIndex);
            if (cityNode == NULL) continue;

            char cityName[MAX_CITY_NAME_LENGTH];
            readJsonStringField(cityNode, "city_name", cityName, sizeof(cityName));

            struct json_object *productsArray = NULL;
            if (!json_object_object_get_ex(cityNode, "products", &productsArray)
                || productsArray == NULL) {
                continue;
            }

            size_t productCount = json_object_array_length(productsArray);
            for (size_t productIndex = 0; productIndex < productCount; productIndex++) {
                struct json_object *productNode =
                    json_object_array_get_idx(productsArray, productIndex);
                if (productNode == NULL) continue;

                ProductRecord record;
                if (populateProductRecordFromJson(productNode, countryName, cityName, &record)) {
                    productRecordListAppend(outList, &record);
                }
            }
        }
    }

    json_object_put(rootArray);
    return true;
}

/* ===========================================================================
 * Section 2 - Replacement Selection Sort with run merge
 *
 * The classic external-sort algorithm: a min-heap of fixed capacity holds
 * a sliding window over the input. Items larger than the last emitted value
 * stay in the active heap and feed the current run; items smaller are
 * "frozen" at the back of the buffer and become the seed of the next run.
 *
 * After all runs are produced, we run a k-way merge using another min-heap
 * (of run heads) to obtain the fully sorted output.
 * ======================================================================== */

/* Push the value at `heapBuffer[startIndex]` down so the subtree rooted there
 * satisfies the min-heap property over the prefix [0, heapSize). */
static void siftDownStringHeap(char **heapBuffer, size_t heapSize, size_t startIndex) {
    size_t parentIndex = startIndex;
    while (true) {
        size_t leftChildIndex  = 2 * parentIndex + 1;
        size_t rightChildIndex = 2 * parentIndex + 2;
        size_t smallestIndex   = parentIndex;
        if (leftChildIndex  < heapSize &&
            strcmp(heapBuffer[leftChildIndex],  heapBuffer[smallestIndex]) < 0) {
            smallestIndex = leftChildIndex;
        }
        if (rightChildIndex < heapSize &&
            strcmp(heapBuffer[rightChildIndex], heapBuffer[smallestIndex]) < 0) {
            smallestIndex = rightChildIndex;
        }
        if (smallestIndex == parentIndex) {
            return;
        }
        char *temporaryPointer       = heapBuffer[parentIndex];
        heapBuffer[parentIndex]      = heapBuffer[smallestIndex];
        heapBuffer[smallestIndex]    = temporaryPointer;
        parentIndex                  = smallestIndex;
    }
}

/* Heapify a buffer of strings into a min-heap. */
static void buildStringMinHeap(char **heapBuffer, size_t heapSize) {
    if (heapSize <= 1) return;
    for (size_t reverseIndex = heapSize / 2; reverseIndex > 0; reverseIndex--) {
        siftDownStringHeap(heapBuffer, heapSize, reverseIndex - 1);
    }
}

static void appendStringToRun(StringRun *run, char *value) {
    char **grown = realloc(run->strings, (run->count + 1) * sizeof(char *));
    if (grown == NULL) {
        fprintf(stderr, "Out of memory while extending sorted run\n");
        exit(EXIT_FAILURE);
    }
    run->strings            = grown;
    run->strings[run->count] = value;
    run->count              += 1;
}

static void appendRunToCollection(StringRunCollection *collection, StringRun newRun) {
    StringRun *grown = realloc(collection->runs,
                               (collection->runCount + 1) * sizeof(StringRun));
    if (grown == NULL) {
        fprintf(stderr, "Out of memory while extending run collection\n");
        exit(EXIT_FAILURE);
    }
    collection->runs                       = grown;
    collection->runs[collection->runCount] = newRun;
    collection->runCount                  += 1;
}

/* Print the active heap and the frozen suffix; the gap (if any) is the
 * "drained" zone that no longer holds live items. Used by the
 * step-by-step trace exposed via menu option [8]. */
static void printHeapStateForTrace(char **heapBuffer,
                                   size_t activeHeapSize,
                                   size_t frozenSectionStart,
                                   size_t frozenSectionEnd) {
    printf("    active=[");
    for (size_t slotIndex = 0; slotIndex < activeHeapSize; slotIndex++) {
        if (slotIndex > 0) printf(", ");
        printf("%s", heapBuffer[slotIndex]);
    }
    printf("]");
    if (frozenSectionStart < frozenSectionEnd) {
        printf("  frozen=[");
        for (size_t slotIndex = frozenSectionStart; slotIndex < frozenSectionEnd; slotIndex++) {
            if (slotIndex > frozenSectionStart) printf(", ");
            printf("%s", heapBuffer[slotIndex]);
        }
        printf("]");
    }
    printf("\n");
}

/* Run the Replacement Selection Sort over the given input strings, populating
 * `outRuns->runs` with one StringRun per produced run. The runs collectively
 * own the same string pointers that came in (no duplication).
 *
 * The buffer is partitioned into three zones during execution:
 *   active heap : slots [0, activeHeapSize)
 *   drained gap : slots [activeHeapSize, frozenSectionStart)  - stale, ignore
 *   frozen tail : slots [frozenSectionStart, frozenSectionEnd) - next run seeds
 *
 * "Replace" (next input >= last emitted) keeps the active heap size unchanged.
 * "Freeze" (next input < last emitted) shrinks active by one and grows the
 * frozen tail by one (frozenSectionStart decreases). "Drain" (no more input)
 * shrinks active by one only and leaves a gap above it.
 *
 * When activeHeapSize hits 0 we emit the run, compact the frozen tail down to
 * the start of the buffer, and re-heapify it as the seed of the next run. */
static void replacementSelectionSortStrings(char **inputStrings, size_t inputCount,
                                            size_t heapCapacity, bool printTrace,
                                            StringRunCollection *outRuns) {
    outRuns->runs     = NULL;
    outRuns->runCount = 0;
    if (inputCount == 0 || heapCapacity == 0) return;

    char **heapBuffer = calloc(heapCapacity, sizeof(char *));
    if (heapBuffer == NULL) {
        fprintf(stderr, "Out of memory allocating RSS heap buffer\n");
        exit(EXIT_FAILURE);
    }

    size_t inputCursor      = 0;
    size_t initialFillCount = (inputCount < heapCapacity) ? inputCount : heapCapacity;
    for (size_t fillIndex = 0; fillIndex < initialFillCount; fillIndex++) {
        heapBuffer[fillIndex] = inputStrings[inputCursor++];
    }
    size_t activeHeapSize      = initialFillCount;
    size_t frozenSectionStart  = initialFillCount;
    size_t frozenSectionEnd    = initialFillCount;
    buildStringMinHeap(heapBuffer, activeHeapSize);

    StringRun currentRun     = { .strings = NULL, .count = 0 };
    char     *lastEmitted    = NULL;
    int       runNumber      = 1;

    if (printTrace) {
        printf("  Replacement Selection Sort - heap capacity %zu, %zu input items\n",
               heapCapacity, inputCount);
        printf("  Initial heap after filling and heapify:\n");
        printHeapStateForTrace(heapBuffer, activeHeapSize,
                               frozenSectionStart, frozenSectionEnd);
        printf("  Run %d begins\n", runNumber);
    }

    while (activeHeapSize > 0) {
        char *minimumString = heapBuffer[0];
        appendStringToRun(&currentRun, minimumString);
        lastEmitted = minimumString;
        if (printTrace) printf("    emit  -> %s\n", minimumString);

        bool  inputExhausted = (inputCursor >= inputCount);
        char *nextInput      = inputExhausted ? NULL : inputStrings[inputCursor++];

        if (nextInput == NULL) {
            /* Drain: pull the last active slot up to the root and shrink. */
            heapBuffer[0] = heapBuffer[activeHeapSize - 1];
            activeHeapSize -= 1;
            if (activeHeapSize > 0) siftDownStringHeap(heapBuffer, activeHeapSize, 0);
        } else if (strcmp(nextInput, lastEmitted) >= 0) {
            /* Replace: new value belongs in the current run. */
            heapBuffer[0] = nextInput;
            siftDownStringHeap(heapBuffer, activeHeapSize, 0);
        } else {
            /* Freeze: new value belongs in the next run.
             * Move the last active slot to the root, place the frozen item
             * just above the new active boundary, and shrink active. */
            heapBuffer[0] = heapBuffer[activeHeapSize - 1];
            activeHeapSize       -= 1;
            frozenSectionStart   -= 1;
            heapBuffer[frozenSectionStart] = nextInput;
            if (activeHeapSize > 0) siftDownStringHeap(heapBuffer, activeHeapSize, 0);
            if (printTrace) {
                printf("    freeze -> %s (smaller than last emitted '%s')\n",
                       nextInput, lastEmitted);
            }
        }

        if (printTrace) {
            printHeapStateForTrace(heapBuffer, activeHeapSize,
                                   frozenSectionStart, frozenSectionEnd);
        }

        if (activeHeapSize == 0) {
            appendRunToCollection(outRuns, currentRun);
            currentRun.strings = NULL;
            currentRun.count   = 0;
            if (printTrace) {
                printf("  Run %d ends with %zu items\n",
                       runNumber, outRuns->runs[outRuns->runCount - 1].count);
            }

            size_t frozenItemCount = frozenSectionEnd - frozenSectionStart;
            if (frozenItemCount > 0) {
                /* Compact the frozen tail down to the front of the buffer. */
                for (size_t copyIndex = 0; copyIndex < frozenItemCount; copyIndex++) {
                    heapBuffer[copyIndex] = heapBuffer[frozenSectionStart + copyIndex];
                }
                activeHeapSize     = frozenItemCount;
                frozenSectionStart = frozenItemCount;
                frozenSectionEnd   = frozenItemCount;
                buildStringMinHeap(heapBuffer, activeHeapSize);
                lastEmitted = NULL;
                runNumber  += 1;
                if (printTrace) {
                    printf("  Run %d begins (reseeded from %zu frozen items)\n",
                           runNumber, activeHeapSize);
                    printHeapStateForTrace(heapBuffer, activeHeapSize,
                                           frozenSectionStart, frozenSectionEnd);
                }
            }
        }
    }

    free(heapBuffer);
}

/* k-way merge of pre-sorted runs, using a min-heap of (run head value, run id)
 * pairs. Output is a freshly allocated array of `char *` pointers borrowed
 * from the runs (caller must not free the strings twice). */
static void mergeStringRunsIntoSortedArray(const StringRunCollection *runs,
                                           char ***outSortedStrings, size_t *outSortedCount) {
    size_t totalCount = 0;
    for (size_t runIndex = 0; runIndex < runs->runCount; runIndex++) {
        totalCount += runs->runs[runIndex].count;
    }
    *outSortedCount = totalCount;
    if (totalCount == 0) {
        *outSortedStrings = NULL;
        return;
    }
    *outSortedStrings = calloc(totalCount, sizeof(char *));
    if (*outSortedStrings == NULL) {
        fprintf(stderr, "Out of memory allocating merge output\n");
        exit(EXIT_FAILURE);
    }

    size_t *runHeadCursors = calloc(runs->runCount, sizeof(size_t));
    if (runHeadCursors == NULL) {
        fprintf(stderr, "Out of memory allocating run cursors\n");
        exit(EXIT_FAILURE);
    }

    size_t outputCursor = 0;
    while (outputCursor < totalCount) {
        size_t      bestRunIndex = SIZE_MAX;
        const char *bestString   = NULL;
        for (size_t runIndex = 0; runIndex < runs->runCount; runIndex++) {
            const StringRun *run = &runs->runs[runIndex];
            if (runHeadCursors[runIndex] >= run->count) continue;
            const char *candidate = run->strings[runHeadCursors[runIndex]];
            if (bestString == NULL || strcmp(candidate, bestString) < 0) {
                bestString   = candidate;
                bestRunIndex = runIndex;
            }
        }
        (*outSortedStrings)[outputCursor++] = (char *)bestString;
        runHeadCursors[bestRunIndex]       += 1;
    }

    free(runHeadCursors);
}

static void releaseStringRunCollection(StringRunCollection *runs, bool freeStringMemory) {
    for (size_t runIndex = 0; runIndex < runs->runCount; runIndex++) {
        if (freeStringMemory) {
            for (size_t stringIndex = 0;
                 stringIndex < runs->runs[runIndex].count;
                 stringIndex++) {
                free(runs->runs[runIndex].strings[stringIndex]);
            }
        }
        free(runs->runs[runIndex].strings);
    }
    free(runs->runs);
    runs->runs     = NULL;
    runs->runCount = 0;
}

static void freeStringArray(char **strings, size_t count) {
    if (strings == NULL) return;
    for (size_t stringIndex = 0; stringIndex < count; stringIndex++) {
        free(strings[stringIndex]);
    }
    free(strings);
}

/* Return the distinct strings from `strings` in encounter order (first
 * occurrence wins). The returned array contains freshly allocated copies and
 * must be released with freeStringArray. */
static char **extractDistinctStringsInEncounterOrder(char **strings, size_t count,
                                                     size_t *outDistinctCount) {
    char **distinct       = NULL;
    size_t distinctCount  = 0;
    for (size_t inputIndex = 0; inputIndex < count; inputIndex++) {
        bool alreadySeen  = false;
        for (size_t seenIndex = 0; seenIndex < distinctCount; seenIndex++) {
            if (strcmp(distinct[seenIndex], strings[inputIndex]) == 0) {
                alreadySeen = true;
                break;
            }
        }
        if (alreadySeen) continue;
        char **grown = realloc(distinct, (distinctCount + 1) * sizeof(char *));
        if (grown == NULL) {
            fprintf(stderr, "Out of memory extracting distinct strings\n");
            exit(EXIT_FAILURE);
        }
        distinct                 = grown;
        distinct[distinctCount]  = strdup(strings[inputIndex]);
        distinctCount           += 1;
    }
    *outDistinctCount = distinctCount;
    return distinct;
}

/* Build the Country Index by extracting distinct country names from the
 * loaded product list, sorting them via Replacement Selection Sort + run
 * merge, and writing them physically sorted into `country_index.dat`. The
 * `firstCityRowOffset` field is initialised to -1 here and patched later
 * by Phase 3 once the City Index exists. */
static void buildAndWriteCountryIndexFile(const ProductRecordList *productList,
                                          const char *countryIndexFilePath) {
    char **countryNamesInEncounterOrder = calloc(productList->count, sizeof(char *));
    if (countryNamesInEncounterOrder == NULL) {
        fprintf(stderr, "Out of memory while collecting country names\n");
        exit(EXIT_FAILURE);
    }
    for (size_t recordIndex = 0; recordIndex < productList->count; recordIndex++) {
        countryNamesInEncounterOrder[recordIndex] = (char *)productList->items[recordIndex].countryName;
    }
    size_t distinctCountryCount = 0;
    char **distinctCountryNames = extractDistinctStringsInEncounterOrder(
        countryNamesInEncounterOrder, productList->count, &distinctCountryCount);
    free(countryNamesInEncounterOrder);

    StringRunCollection runs;
    replacementSelectionSortStrings(distinctCountryNames, distinctCountryCount,
                                    REPLACEMENT_SELECTION_HEAP_SIZE, false, &runs);

    char **sortedCountryNames = NULL;
    size_t sortedCountryCount = 0;
    mergeStringRunsIntoSortedArray(&runs, &sortedCountryNames, &sortedCountryCount);

    FILE *countryIndexFile = fopen(countryIndexFilePath, "wb");
    if (countryIndexFile == NULL) {
        fprintf(stderr, "Cannot open '%s' for writing: %s\n",
                countryIndexFilePath, strerror(errno));
        exit(EXIT_FAILURE);
    }
    for (size_t entryIndex = 0; entryIndex < sortedCountryCount; entryIndex++) {
        CountryIndexEntry entry;
        memset(&entry, 0, sizeof(entry));
        copyStringIntoFixedBuffer(entry.countryName, sizeof(entry.countryName),
                                  sortedCountryNames[entryIndex]);
        entry.firstCityRowOffset = END_OF_LIST_SENTINEL;
        if (fwrite(&entry, sizeof(entry), 1, countryIndexFile) != 1) {
            fprintf(stderr, "Failed writing country index entry %zu\n", entryIndex);
            exit(EXIT_FAILURE);
        }
    }
    fclose(countryIndexFile);

    free(sortedCountryNames);            /* pointers borrowed from runs */
    releaseStringRunCollection(&runs, false);
    freeStringArray(distinctCountryNames, distinctCountryCount);
}

/* ===========================================================================
 * Section 4 - City Index
 *
 * For every country (in the order written to the country index) we collect
 * distinct city names in JSON encounter order, append a CityIndexEntry per
 * city to city_index.dat, then patch:
 *   1. each country entry's `firstCityRowOffset` to point at the
 *      alphabetically-first city of that country;
 *   2. each city's `nextCityRowOffset` to form an alphabetical linked list
 *      that ends with the END_OF_LIST_SENTINEL.
 *
 * The physical order in city_index.dat purposely differs from alphabetical
 * order - this is the exact pattern shown in the PDF example.
 * ======================================================================== */

static long findCityRowNumberByName(CityIndexEntry *cityEntries, size_t cityCount,
                                    const char *cityName, const char *countryName) {
    for (size_t entryIndex = 0; entryIndex < cityCount; entryIndex++) {
        if (strcmp(cityEntries[entryIndex].cityName, cityName) == 0 &&
            strcmp(cityEntries[entryIndex].owningCountryName, countryName) == 0) {
            return (long)(entryIndex + 1);   /* row numbers are 1-based */
        }
    }
    return END_OF_LIST_SENTINEL;
}

static void buildAndWriteCityIndexFile(const ProductRecordList *productList,
                                       const char *countryIndexFilePath,
                                       const char *cityIndexFilePath) {
    /* Load the country index into memory so we can patch firstCityRowOffset. */
    FILE *countryIndexInputFile = fopen(countryIndexFilePath, "rb");
    if (countryIndexInputFile == NULL) {
        fprintf(stderr, "Cannot open '%s' for reading: %s\n",
                countryIndexFilePath, strerror(errno));
        exit(EXIT_FAILURE);
    }
    fseek(countryIndexInputFile, 0, SEEK_END);
    long   countryIndexFileSize = ftell(countryIndexInputFile);
    size_t countryEntryCount    = (size_t)countryIndexFileSize / sizeof(CountryIndexEntry);
    fseek(countryIndexInputFile, 0, SEEK_SET);
    CountryIndexEntry *countryEntries = calloc(countryEntryCount, sizeof(CountryIndexEntry));
    if (countryEntries == NULL) {
        fprintf(stderr, "Out of memory loading country index\n");
        exit(EXIT_FAILURE);
    }
    if (fread(countryEntries, sizeof(CountryIndexEntry),
              countryEntryCount, countryIndexInputFile) != countryEntryCount) {
        fprintf(stderr, "Truncated country index file\n");
        exit(EXIT_FAILURE);
    }
    fclose(countryIndexInputFile);

    /* Build the in-memory city index by visiting each country in
     * country-index order and collecting distinct cities in JSON
     * encounter order. */
    CityIndexEntry *cityEntries = NULL;
    size_t          cityCount   = 0;

    for (size_t countryIndex = 0; countryIndex < countryEntryCount; countryIndex++) {
        const char *currentCountryName = countryEntries[countryIndex].countryName;

        /* Gather distinct cities for this country in JSON encounter order. */
        char **citiesInEncounterOrder = calloc(productList->count, sizeof(char *));
        if (citiesInEncounterOrder == NULL) {
            fprintf(stderr, "Out of memory collecting city names\n");
            exit(EXIT_FAILURE);
        }
        size_t collectedCount = 0;
        for (size_t recordIndex = 0; recordIndex < productList->count; recordIndex++) {
            if (strcmp(productList->items[recordIndex].countryName, currentCountryName) != 0) {
                continue;
            }
            citiesInEncounterOrder[collectedCount++] =
                (char *)productList->items[recordIndex].cityName;
        }

        size_t distinctCityCount = 0;
        char **distinctCityNames = extractDistinctStringsInEncounterOrder(
            citiesInEncounterOrder, collectedCount, &distinctCityCount);
        free(citiesInEncounterOrder);

        /* Append city entries in encounter order to the in-memory list. */
        cityEntries = realloc(cityEntries,
                              (cityCount + distinctCityCount) * sizeof(CityIndexEntry));
        if (cityEntries == NULL && distinctCityCount > 0) {
            fprintf(stderr, "Out of memory growing city index\n");
            exit(EXIT_FAILURE);
        }
        for (size_t addIndex = 0; addIndex < distinctCityCount; addIndex++) {
            CityIndexEntry *entry = &cityEntries[cityCount + addIndex];
            memset(entry, 0, sizeof(*entry));
            copyStringIntoFixedBuffer(entry->cityName, sizeof(entry->cityName),
                                      distinctCityNames[addIndex]);
            copyStringIntoFixedBuffer(entry->owningCountryName,
                                      sizeof(entry->owningCountryName),
                                      currentCountryName);
            entry->nextCityRowOffset     = END_OF_LIST_SENTINEL;
            entry->firstProductRowOffset = END_OF_LIST_SENTINEL;
        }
        cityCount += distinctCityCount;

        /* Sort the city names alphabetically using RSS + run merge, then
         * walk the alphabetical sequence to build the linked list. */
        StringRunCollection runs;
        replacementSelectionSortStrings(distinctCityNames, distinctCityCount,
                                        REPLACEMENT_SELECTION_HEAP_SIZE, false, &runs);
        char **alphabeticallySortedCityNames = NULL;
        size_t alphabeticallySortedCount     = 0;
        mergeStringRunsIntoSortedArray(&runs, &alphabeticallySortedCityNames,
                                       &alphabeticallySortedCount);

        if (alphabeticallySortedCount > 0) {
            long firstAlphabeticalRow = findCityRowNumberByName(
                cityEntries, cityCount,
                alphabeticallySortedCityNames[0], currentCountryName);
            countryEntries[countryIndex].firstCityRowOffset = firstAlphabeticalRow;

            for (size_t orderIndex = 0;
                 orderIndex + 1 < alphabeticallySortedCount;
                 orderIndex++) {
                long currentRow = findCityRowNumberByName(
                    cityEntries, cityCount,
                    alphabeticallySortedCityNames[orderIndex], currentCountryName);
                long nextRow    = findCityRowNumberByName(
                    cityEntries, cityCount,
                    alphabeticallySortedCityNames[orderIndex + 1], currentCountryName);
                cityEntries[currentRow - 1].nextCityRowOffset = nextRow;
            }
        }

        free(alphabeticallySortedCityNames);
        releaseStringRunCollection(&runs, false);
        freeStringArray(distinctCityNames, distinctCityCount);
    }

    /* Persist the city index. */
    FILE *cityIndexOutputFile = fopen(cityIndexFilePath, "wb");
    if (cityIndexOutputFile == NULL) {
        fprintf(stderr, "Cannot open '%s' for writing: %s\n",
                cityIndexFilePath, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (cityCount > 0 &&
        fwrite(cityEntries, sizeof(CityIndexEntry), cityCount, cityIndexOutputFile) != cityCount) {
        fprintf(stderr, "Failed writing city index\n");
        exit(EXIT_FAILURE);
    }
    fclose(cityIndexOutputFile);

    /* Rewrite the country index with patched firstCityRowOffset values. */
    FILE *countryIndexOutputFile = fopen(countryIndexFilePath, "wb");
    if (countryIndexOutputFile == NULL) {
        fprintf(stderr, "Cannot reopen '%s' for writing: %s\n",
                countryIndexFilePath, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (fwrite(countryEntries, sizeof(CountryIndexEntry),
               countryEntryCount, countryIndexOutputFile) != countryEntryCount) {
        fprintf(stderr, "Failed rewriting country index\n");
        exit(EXIT_FAILURE);
    }
    fclose(countryIndexOutputFile);

    free(cityEntries);
    free(countryEntries);
}

/* ===========================================================================
 * Section 4b - Product Index + data file (Level 3 + Level 4)
 *
 * For every city (in physical row order in city_index.dat) we:
 *   1. Collect the products belonging to that city in JSON encounter order;
 *   2. Append every ProductRecord to products.dat (Level 4), recording its
 *      byte offset;
 *   3. Append a ProductIndexEntry to product_index.dat for each product, in
 *      the same order, with the byte offset above as `dataFileRecordOffset`
 *      and `nextProductRowOffset = -1`;
 *   4. Sort the product names alphabetically with RSS + run merge;
 *   5. Patch `nextProductRowOffset` to form the alphabetical chain and set
 *      the city's `firstProductRowOffset` to the alphabetically first row.
 * Finally we rewrite city_index.dat so its firstProductRowOffset values are
 * persistent.
 * ======================================================================== */

static long findProductRowNumberByName(ProductIndexEntry *productEntries, size_t productCount,
                                       const char *productName, const char *cityName) {
    for (size_t entryIndex = 0; entryIndex < productCount; entryIndex++) {
        if (strcmp(productEntries[entryIndex].productName,    productName) == 0 &&
            strcmp(productEntries[entryIndex].owningCityName, cityName)    == 0) {
            return (long)(entryIndex + 1);
        }
    }
    return END_OF_LIST_SENTINEL;
}

static void buildAndWriteProductIndexAndDataFile(const ProductRecordList *productList,
                                                 const char *cityIndexFilePath,
                                                 const char *productIndexFilePath,
                                                 const char *dataFilePath) {
    /* Load the city index into memory so we can patch firstProductRowOffset. */
    size_t          cityCount   = 0;
    CityIndexEntry *cityEntries = NULL;
    {
        FILE *cityIndexInputFile = fopen(cityIndexFilePath, "rb");
        if (cityIndexInputFile == NULL) {
            fprintf(stderr, "Cannot open '%s' for reading: %s\n",
                    cityIndexFilePath, strerror(errno));
            exit(EXIT_FAILURE);
        }
        fseek(cityIndexInputFile, 0, SEEK_END);
        cityCount = (size_t)ftell(cityIndexInputFile) / sizeof(CityIndexEntry);
        fseek(cityIndexInputFile, 0, SEEK_SET);
        cityEntries = calloc(cityCount, sizeof(CityIndexEntry));
        if (cityEntries == NULL) {
            fprintf(stderr, "Out of memory loading city index\n");
            exit(EXIT_FAILURE);
        }
        if (fread(cityEntries, sizeof(CityIndexEntry), cityCount, cityIndexInputFile) != cityCount) {
            fprintf(stderr, "Truncated city index file\n");
            exit(EXIT_FAILURE);
        }
        fclose(cityIndexInputFile);
    }

    FILE *dataFileOutput = fopen(dataFilePath, "wb");
    if (dataFileOutput == NULL) {
        fprintf(stderr, "Cannot open '%s' for writing: %s\n",
                dataFilePath, strerror(errno));
        exit(EXIT_FAILURE);
    }

    ProductIndexEntry *productEntries = NULL;
    size_t             productEntryCount = 0;

    for (size_t cityRowIndex = 0; cityRowIndex < cityCount; cityRowIndex++) {
        const CityIndexEntry *currentCityEntry = &cityEntries[cityRowIndex];

        /* Gather products for this city in JSON encounter order. */
        size_t collectedProductCount = 0;
        for (size_t recordIndex = 0; recordIndex < productList->count; recordIndex++) {
            const ProductRecord *record = &productList->items[recordIndex];
            if (strcmp(record->cityName,    currentCityEntry->cityName)         == 0 &&
                strcmp(record->countryName, currentCityEntry->owningCountryName) == 0) {
                collectedProductCount += 1;
            }
        }
        char **productNamesInEncounterOrder = calloc(collectedProductCount, sizeof(char *));
        if (productNamesInEncounterOrder == NULL && collectedProductCount > 0) {
            fprintf(stderr, "Out of memory collecting product names\n");
            exit(EXIT_FAILURE);
        }
        size_t collectedSoFar = 0;

        productEntries = realloc(productEntries,
                                 (productEntryCount + collectedProductCount) * sizeof(ProductIndexEntry));
        if (productEntries == NULL && (productEntryCount + collectedProductCount) > 0) {
            fprintf(stderr, "Out of memory growing product index\n");
            exit(EXIT_FAILURE);
        }

        for (size_t recordIndex = 0; recordIndex < productList->count; recordIndex++) {
            const ProductRecord *record = &productList->items[recordIndex];
            if (strcmp(record->cityName,    currentCityEntry->cityName)         != 0 ||
                strcmp(record->countryName, currentCityEntry->owningCountryName) != 0) {
                continue;
            }
            long byteOffsetInDataFile = ftell(dataFileOutput);
            if (fwrite(record, sizeof(ProductRecord), 1, dataFileOutput) != 1) {
                fprintf(stderr, "Failed writing product record\n");
                exit(EXIT_FAILURE);
            }
            ProductIndexEntry *entry = &productEntries[productEntryCount];
            memset(entry, 0, sizeof(*entry));
            copyStringIntoFixedBuffer(entry->productName, sizeof(entry->productName),
                                      record->productName);
            copyStringIntoFixedBuffer(entry->owningCityName, sizeof(entry->owningCityName),
                                      record->cityName);
            entry->nextProductRowOffset  = END_OF_LIST_SENTINEL;
            entry->dataFileRecordOffset  = byteOffsetInDataFile;
            productNamesInEncounterOrder[collectedSoFar++] = (char *)record->productName;
            productEntryCount += 1;
        }

        /* Sort product names alphabetically and rebuild the chain. */
        StringRunCollection runs;
        replacementSelectionSortStrings(productNamesInEncounterOrder, collectedProductCount,
                                        REPLACEMENT_SELECTION_HEAP_SIZE, false, &runs);
        char **alphabeticallySortedProductNames = NULL;
        size_t alphabeticallySortedProductCount = 0;
        mergeStringRunsIntoSortedArray(&runs, &alphabeticallySortedProductNames,
                                       &alphabeticallySortedProductCount);

        if (alphabeticallySortedProductCount > 0) {
            long firstAlphabeticalRow = findProductRowNumberByName(
                productEntries, productEntryCount,
                alphabeticallySortedProductNames[0], currentCityEntry->cityName);
            cityEntries[cityRowIndex].firstProductRowOffset = firstAlphabeticalRow;

            for (size_t orderIndex = 0;
                 orderIndex + 1 < alphabeticallySortedProductCount;
                 orderIndex++) {
                long currentRow = findProductRowNumberByName(
                    productEntries, productEntryCount,
                    alphabeticallySortedProductNames[orderIndex],
                    currentCityEntry->cityName);
                long nextRow    = findProductRowNumberByName(
                    productEntries, productEntryCount,
                    alphabeticallySortedProductNames[orderIndex + 1],
                    currentCityEntry->cityName);
                productEntries[currentRow - 1].nextProductRowOffset = nextRow;
            }
        }

        free(alphabeticallySortedProductNames);
        releaseStringRunCollection(&runs, false);
        free(productNamesInEncounterOrder);
    }

    fclose(dataFileOutput);

    /* Persist the product index. */
    FILE *productIndexOutputFile = fopen(productIndexFilePath, "wb");
    if (productIndexOutputFile == NULL) {
        fprintf(stderr, "Cannot open '%s' for writing: %s\n",
                productIndexFilePath, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (productEntryCount > 0 &&
        fwrite(productEntries, sizeof(ProductIndexEntry),
               productEntryCount, productIndexOutputFile) != productEntryCount) {
        fprintf(stderr, "Failed writing product index\n");
        exit(EXIT_FAILURE);
    }
    fclose(productIndexOutputFile);

    /* Rewrite the city index with patched firstProductRowOffset values. */
    FILE *cityIndexOutputFile = fopen(cityIndexFilePath, "wb");
    if (cityIndexOutputFile == NULL) {
        fprintf(stderr, "Cannot reopen '%s' for writing: %s\n",
                cityIndexFilePath, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (fwrite(cityEntries, sizeof(CityIndexEntry), cityCount, cityIndexOutputFile) != cityCount) {
        fprintf(stderr, "Failed rewriting city index\n");
        exit(EXIT_FAILURE);
    }
    fclose(cityIndexOutputFile);

    free(productEntries);
    free(cityEntries);
}

/* ===========================================================================
 * Section 5 - Index-only access session, search and traversal
 *
 * Holds open FILE * handles for the four binary files plus their entry counts.
 * Every read goes through one of the readXxxAt(...) helpers so tests can
 * count exactly how many records were touched - this is how we prove that
 * search operations never perform a full linear scan of products.dat.
 * ======================================================================== */

typedef struct {
    FILE   *countryIndexFile;
    FILE   *cityIndexFile;
    FILE   *productIndexFile;
    FILE   *dataFile;
    size_t  countryEntryCount;
    size_t  cityEntryCount;
    size_t  productEntryCount;
    long    dataFileReadCount;
} IndexAccessSession;

static long fileSizeInEntries(FILE *file, size_t entrySize) {
    long currentPosition = ftell(file);
    fseek(file, 0, SEEK_END);
    long totalSize = ftell(file);
    fseek(file, currentPosition, SEEK_SET);
    return totalSize / (long)entrySize;
}

static bool openIndexAccessSession(IndexAccessSession *session,
                                   const char *countryIndexFilePath,
                                   const char *cityIndexFilePath,
                                   const char *productIndexFilePath,
                                   const char *dataFilePath) {
    memset(session, 0, sizeof(*session));
    session->countryIndexFile = fopen(countryIndexFilePath, "rb+");
    session->cityIndexFile    = fopen(cityIndexFilePath,    "rb+");
    session->productIndexFile = fopen(productIndexFilePath, "rb+");
    session->dataFile         = fopen(dataFilePath,         "rb+");
    if (session->countryIndexFile == NULL || session->cityIndexFile    == NULL ||
        session->productIndexFile == NULL || session->dataFile         == NULL) {
        return false;
    }
    session->countryEntryCount = (size_t)fileSizeInEntries(
        session->countryIndexFile, sizeof(CountryIndexEntry));
    session->cityEntryCount    = (size_t)fileSizeInEntries(
        session->cityIndexFile,    sizeof(CityIndexEntry));
    session->productEntryCount = (size_t)fileSizeInEntries(
        session->productIndexFile, sizeof(ProductIndexEntry));
    return true;
}

static void closeIndexAccessSession(IndexAccessSession *session) {
    if (session->countryIndexFile != NULL) fclose(session->countryIndexFile);
    if (session->cityIndexFile    != NULL) fclose(session->cityIndexFile);
    if (session->productIndexFile != NULL) fclose(session->productIndexFile);
    if (session->dataFile         != NULL) fclose(session->dataFile);
    memset(session, 0, sizeof(*session));
}

static bool readCountryEntryAtRow(IndexAccessSession *session, long rowNumber,
                                  CountryIndexEntry *outEntry) {
    if (rowNumber < 1 || (size_t)rowNumber > session->countryEntryCount) return false;
    fseek(session->countryIndexFile,
          (rowNumber - 1) * (long)sizeof(CountryIndexEntry), SEEK_SET);
    return fread(outEntry, sizeof(*outEntry), 1, session->countryIndexFile) == 1;
}

static bool readCityEntryAtRow(IndexAccessSession *session, long rowNumber,
                               CityIndexEntry *outEntry) {
    if (rowNumber < 1 || (size_t)rowNumber > session->cityEntryCount) return false;
    fseek(session->cityIndexFile,
          (rowNumber - 1) * (long)sizeof(CityIndexEntry), SEEK_SET);
    return fread(outEntry, sizeof(*outEntry), 1, session->cityIndexFile) == 1;
}

static bool readProductEntryAtRow(IndexAccessSession *session, long rowNumber,
                                  ProductIndexEntry *outEntry) {
    if (rowNumber < 1 || (size_t)rowNumber > session->productEntryCount) return false;
    fseek(session->productIndexFile,
          (rowNumber - 1) * (long)sizeof(ProductIndexEntry), SEEK_SET);
    return fread(outEntry, sizeof(*outEntry), 1, session->productIndexFile) == 1;
}

static bool readDataRecordAtByteOffset(IndexAccessSession *session, long byteOffset,
                                       ProductRecord *outRecord) {
    fseek(session->dataFile, byteOffset, SEEK_SET);
    bool ok = fread(outRecord, sizeof(*outRecord), 1, session->dataFile) == 1;
    if (ok) session->dataFileReadCount += 1;
    return ok;
}

/* Binary search the (physically sorted) country index for an exact name match.
 * Returns the 1-based row number, or END_OF_LIST_SENTINEL if not found. */
static long findCountryRowByExactName(IndexAccessSession *session, const char *countryName,
                                      bool printTrace) {
    long lowRow  = 1;
    long highRow = (long)session->countryEntryCount;
    while (lowRow <= highRow) {
        long midRow = lowRow + (highRow - lowRow) / 2;
        CountryIndexEntry midEntry;
        if (!readCountryEntryAtRow(session, midRow, &midEntry)) {
            return END_OF_LIST_SENTINEL;
        }
        int comparison = strcmp(countryName, midEntry.countryName);
        if (printTrace) {
            printf("    country index row [%ld]: read field 'countryName' = '%s' "
                   "(strcmp(target,'%s')=%d)\n",
                   midRow, midEntry.countryName, midEntry.countryName, comparison);
        }
        if (comparison == 0) return midRow;
        if (comparison <  0) highRow = midRow - 1;
        else                  lowRow  = midRow + 1;
    }
    return END_OF_LIST_SENTINEL;
}

/* Print one product record as a single user-facing line. */
static void printProductRecordLine(const ProductRecord *record) {
    printf("    %-13s | %-12s | %-15s | %-32s | %4d %s | stock %3d\n",
           record->productIdentifier,
           record->countryName,
           record->cityName,
           record->productName,
           record->priceAmount,
           record->priceCurrency,
           record->inventoryStock);
}

/* Walk every product of a city in alphabetical order, printing each record.
 * Returns the number of products visited. */
static size_t walkCityProductChainAndPrint(IndexAccessSession *session,
                                           const CityIndexEntry *cityEntry,
                                           bool printTrace) {
    size_t productsVisited = 0;
    long   currentRow      = cityEntry->firstProductRowOffset;
    while (currentRow != END_OF_LIST_SENTINEL) {
        ProductIndexEntry productEntry;
        if (!readProductEntryAtRow(session, currentRow, &productEntry)) break;
        if (printTrace) {
            printf("      product index row [%ld]: name='%s', dataOffset=%ld, next=%ld\n",
                   currentRow, productEntry.productName,
                   productEntry.dataFileRecordOffset,
                   productEntry.nextProductRowOffset);
        }
        ProductRecord record;
        if (readDataRecordAtByteOffset(session, productEntry.dataFileRecordOffset, &record)) {
            printProductRecordLine(&record);
            productsVisited += 1;
        }
        currentRow = productEntry.nextProductRowOffset;
    }
    return productsVisited;
}

/* List all products in a country. Prints a step-by-step trace if asked. */
static size_t searchAndPrintByCountry(IndexAccessSession *session, const char *countryName,
                                      bool printTrace) {
    if (printTrace) {
        printf("  Step 1: binary search the Country Index for '%s'\n", countryName);
    }
    long countryRow = findCountryRowByExactName(session, countryName, printTrace);
    if (countryRow == END_OF_LIST_SENTINEL) {
        printf("  No country named '%s' was found.\n", countryName);
        return 0;
    }

    CountryIndexEntry countryEntry;
    readCountryEntryAtRow(session, countryRow, &countryEntry);
    if (printTrace) {
        printf("  Step 2: follow firstCityRowOffset = %ld\n",
               countryEntry.firstCityRowOffset);
    }

    size_t totalProducts = 0;
    long   cityRow       = countryEntry.firstCityRowOffset;
    while (cityRow != END_OF_LIST_SENTINEL) {
        CityIndexEntry cityEntry;
        if (!readCityEntryAtRow(session, cityRow, &cityEntry)) break;
        if (printTrace) {
            printf("    city index row [%ld]: name='%s', firstProduct=%ld, next=%ld\n",
                   cityRow, cityEntry.cityName,
                   cityEntry.firstProductRowOffset, cityEntry.nextCityRowOffset);
        }
        printf("  City: %s\n", cityEntry.cityName);
        totalProducts += walkCityProductChainAndPrint(session, &cityEntry, printTrace);
        cityRow = cityEntry.nextCityRowOffset;
    }
    printf("  Total %zu products listed for country '%s'\n", totalProducts, countryName);
    return totalProducts;
}

/* List all products in a city (must specify country to disambiguate name
 * collisions across countries; for this dataset there are no collisions
 * but the path remains correct in general). */
static size_t searchAndPrintByCity(IndexAccessSession *session, const char *cityName,
                                   bool printTrace) {
    if (printTrace) {
        printf("  Step 1: walk the Country Index, follow each country's city chain "
               "until cityName='%s' is found\n", cityName);
    }
    for (long countryRow = 1; (size_t)countryRow <= session->countryEntryCount; countryRow++) {
        CountryIndexEntry countryEntry;
        readCountryEntryAtRow(session, countryRow, &countryEntry);
        if (printTrace) {
            printf("    country index row [%ld]: name='%s', firstCity=%ld\n",
                   countryRow, countryEntry.countryName, countryEntry.firstCityRowOffset);
        }
        long cityRow = countryEntry.firstCityRowOffset;
        while (cityRow != END_OF_LIST_SENTINEL) {
            CityIndexEntry cityEntry;
            if (!readCityEntryAtRow(session, cityRow, &cityEntry)) break;
            if (strcmp(cityEntry.cityName, cityName) == 0) {
                if (printTrace) {
                    printf("    matched city at row [%ld] in country '%s'\n",
                           cityRow, countryEntry.countryName);
                }
                printf("  City: %s (%s)\n", cityEntry.cityName, cityEntry.owningCountryName);
                size_t found = walkCityProductChainAndPrint(session, &cityEntry, printTrace);
                printf("  Total %zu products listed for city '%s'\n", found, cityName);
                return found;
            }
            cityRow = cityEntry.nextCityRowOffset;
        }
    }
    printf("  No city named '%s' was found.\n", cityName);
    return 0;
}

/* List every product whose name matches exactly. Walks the entire hierarchy
 * (still index-only) because there is no global product name index. */
static size_t searchAndPrintByProductName(IndexAccessSession *session,
                                          const char *productName, bool printTrace) {
    if (printTrace) {
        printf("  Step 1: walk every (country, city) chain looking for products "
               "whose name equals '%s'\n", productName);
    }
    size_t totalMatches = 0;
    for (long countryRow = 1; (size_t)countryRow <= session->countryEntryCount; countryRow++) {
        CountryIndexEntry countryEntry;
        readCountryEntryAtRow(session, countryRow, &countryEntry);
        long cityRow = countryEntry.firstCityRowOffset;
        while (cityRow != END_OF_LIST_SENTINEL) {
            CityIndexEntry cityEntry;
            if (!readCityEntryAtRow(session, cityRow, &cityEntry)) break;
            long productRow = cityEntry.firstProductRowOffset;
            while (productRow != END_OF_LIST_SENTINEL) {
                ProductIndexEntry productEntry;
                if (!readProductEntryAtRow(session, productRow, &productEntry)) break;
                if (strcmp(productEntry.productName, productName) == 0) {
                    if (printTrace) {
                        printf("    matched product index row [%ld] in city '%s' "
                               "(country '%s'), dataOffset=%ld\n",
                               productRow, cityEntry.cityName,
                               countryEntry.countryName,
                               productEntry.dataFileRecordOffset);
                    }
                    ProductRecord record;
                    if (readDataRecordAtByteOffset(session,
                                                   productEntry.dataFileRecordOffset, &record)) {
                        printProductRecordLine(&record);
                        totalMatches += 1;
                    }
                }
                productRow = productEntry.nextProductRowOffset;
            }
            cityRow = cityEntry.nextCityRowOffset;
        }
    }
    printf("  Total %zu match(es) for product name '%s'\n", totalMatches, productName);
    return totalMatches;
}

/* Display the Country Index in physical (sorted) order. */
static void displayCountryIndexSorted(IndexAccessSession *session) {
    printf("Country Index (physically sorted via Replacement Selection Sort):\n");
    for (long row = 1; (size_t)row <= session->countryEntryCount; row++) {
        CountryIndexEntry entry;
        readCountryEntryAtRow(session, row, &entry);
        printf("  [%2ld] %-12s firstCityRowOffset=%ld\n",
               row, entry.countryName, entry.firstCityRowOffset);
    }
}

/* Display every country's city chain in alphabetical order via next-pointers. */
static void displayCityIndexAlphabeticalPerCountry(IndexAccessSession *session) {
    printf("City Index (alphabetical traversal per country via next-pointers):\n");
    for (long countryRow = 1; (size_t)countryRow <= session->countryEntryCount; countryRow++) {
        CountryIndexEntry countryEntry;
        readCountryEntryAtRow(session, countryRow, &countryEntry);
        printf("  %s:\n", countryEntry.countryName);
        long cityRow = countryEntry.firstCityRowOffset;
        while (cityRow != END_OF_LIST_SENTINEL) {
            CityIndexEntry cityEntry;
            readCityEntryAtRow(session, cityRow, &cityEntry);
            printf("    [%2ld] %-15s next=%-3ld firstProduct=%ld\n",
                   cityRow, cityEntry.cityName,
                   cityEntry.nextCityRowOffset, cityEntry.firstProductRowOffset);
            cityRow = cityEntry.nextCityRowOffset;
        }
    }
}

/* Display every city's product chain in alphabetical order via next-pointers. */
static void displayProductIndexAlphabeticalPerCity(IndexAccessSession *session) {
    printf("Product Index (alphabetical traversal per city via next-pointers):\n");
    for (long countryRow = 1; (size_t)countryRow <= session->countryEntryCount; countryRow++) {
        CountryIndexEntry countryEntry;
        readCountryEntryAtRow(session, countryRow, &countryEntry);
        long cityRow = countryEntry.firstCityRowOffset;
        while (cityRow != END_OF_LIST_SENTINEL) {
            CityIndexEntry cityEntry;
            readCityEntryAtRow(session, cityRow, &cityEntry);
            printf("  %s / %s:\n", countryEntry.countryName, cityEntry.cityName);
            long productRow = cityEntry.firstProductRowOffset;
            while (productRow != END_OF_LIST_SENTINEL) {
                ProductIndexEntry productEntry;
                readProductEntryAtRow(session, productRow, &productEntry);
                printf("    [%3ld] %-32s next=%-4ld dataOffset=%ld\n",
                       productRow, productEntry.productName,
                       productEntry.nextProductRowOffset,
                       productEntry.dataFileRecordOffset);
                productRow = productEntry.nextProductRowOffset;
            }
            cityRow = cityEntry.nextCityRowOffset;
        }
    }
}

/* ===========================================================================
 * Section 6 - Insert a new product (Task 5)
 *
 * The PDF "Inserting Product A into Berlin" example dictates the behaviour:
 *   - Append the new product record physically to the end of products.dat;
 *   - Append a new ProductIndexEntry physically to the end of the product
 *     index, with dataFileRecordOffset pointing at the new record;
 *   - Walk the target city's alphabetical product chain to find the correct
 *     insertion slot, then splice the new row in by adjusting the
 *     predecessor's nextProductRowOffset and (when needed) the city's
 *     firstProductRowOffset;
 *   - Existing physical positions of D, E, etc. are NEVER moved.
 * ======================================================================== */

typedef enum {
    INSERT_OK,
    INSERT_CITY_NOT_FOUND,
    INSERT_DUPLICATE_NAME_IN_CITY
} InsertProductOutcome;

/* Locate a (city, country) pair in the city index. Returns its 1-based row
 * number, or END_OF_LIST_SENTINEL if no such city exists. This walks the
 * city index only - never the data file. */
static long findCityRowByCityAndCountry(IndexAccessSession *session,
                                        const char *cityName,
                                        const char *countryName) {
    for (long row = 1; (size_t)row <= session->cityEntryCount; row++) {
        CityIndexEntry entry;
        if (!readCityEntryAtRow(session, row, &entry)) break;
        if (strcmp(entry.cityName,         cityName)    == 0 &&
            strcmp(entry.owningCountryName, countryName) == 0) {
            return row;
        }
    }
    return END_OF_LIST_SENTINEL;
}

/* Write a single CityIndexEntry back to the city index file at the given row. */
static void writeCityEntryAtRow(IndexAccessSession *session, long rowNumber,
                                const CityIndexEntry *entry) {
    fseek(session->cityIndexFile,
          (rowNumber - 1) * (long)sizeof(CityIndexEntry), SEEK_SET);
    fwrite(entry, sizeof(*entry), 1, session->cityIndexFile);
    fflush(session->cityIndexFile);
}

/* Write a single ProductIndexEntry back to the product index file at the row. */
static void writeProductEntryAtRow(IndexAccessSession *session, long rowNumber,
                                   const ProductIndexEntry *entry) {
    fseek(session->productIndexFile,
          (rowNumber - 1) * (long)sizeof(ProductIndexEntry), SEEK_SET);
    fwrite(entry, sizeof(*entry), 1, session->productIndexFile);
    fflush(session->productIndexFile);
}

/* Append a record to products.dat and return its byte offset. */
static long appendProductRecordToDataFile(IndexAccessSession *session,
                                          const ProductRecord *record) {
    fseek(session->dataFile, 0, SEEK_END);
    long byteOffset = ftell(session->dataFile);
    fwrite(record, sizeof(*record), 1, session->dataFile);
    fflush(session->dataFile);
    return byteOffset;
}

/* Append a new product index entry (at the physical end of the file) and
 * return its 1-based row number. The new entry's nextProductRowOffset is
 * set to END_OF_LIST_SENTINEL by the caller after splicing. */
static long appendProductIndexEntry(IndexAccessSession *session,
                                    const ProductIndexEntry *entry) {
    fseek(session->productIndexFile, 0, SEEK_END);
    fwrite(entry, sizeof(*entry), 1, session->productIndexFile);
    fflush(session->productIndexFile);
    session->productEntryCount += 1;
    return (long)session->productEntryCount;
}

/* Insert a new product into the city named in `newRecord->cityName`.
 * Performs the splice in-place via pointer updates only. Returns INSERT_OK
 * on success. */
static InsertProductOutcome insertProductIntoCity(IndexAccessSession *session,
                                                  const ProductRecord *newRecord,
                                                  bool printTrace) {
    long cityRow = findCityRowByCityAndCountry(session, newRecord->cityName,
                                               newRecord->countryName);
    if (cityRow == END_OF_LIST_SENTINEL) {
        return INSERT_CITY_NOT_FOUND;
    }

    CityIndexEntry cityEntry;
    readCityEntryAtRow(session, cityRow, &cityEntry);

    /* Reject duplicates: a product with the same name already in this city. */
    {
        long scanRow = cityEntry.firstProductRowOffset;
        while (scanRow != END_OF_LIST_SENTINEL) {
            ProductIndexEntry existingEntry;
            readProductEntryAtRow(session, scanRow, &existingEntry);
            if (strcmp(existingEntry.productName, newRecord->productName) == 0) {
                return INSERT_DUPLICATE_NAME_IN_CITY;
            }
            scanRow = existingEntry.nextProductRowOffset;
        }
    }

    long byteOffset = appendProductRecordToDataFile(session, newRecord);
    if (printTrace) {
        printf("  Appended product record to data file at byte offset %ld\n", byteOffset);
    }

    ProductIndexEntry newEntry;
    memset(&newEntry, 0, sizeof(newEntry));
    copyStringIntoFixedBuffer(newEntry.productName, sizeof(newEntry.productName),
                              newRecord->productName);
    copyStringIntoFixedBuffer(newEntry.owningCityName, sizeof(newEntry.owningCityName),
                              newRecord->cityName);
    newEntry.dataFileRecordOffset = byteOffset;
    newEntry.nextProductRowOffset = END_OF_LIST_SENTINEL;
    long newRow = appendProductIndexEntry(session, &newEntry);
    if (printTrace) {
        printf("  Appended product index entry at row %ld\n", newRow);
    }

    /* Find the alphabetical insertion point. */
    long previousRow = END_OF_LIST_SENTINEL;
    long currentRow  = cityEntry.firstProductRowOffset;
    while (currentRow != END_OF_LIST_SENTINEL) {
        ProductIndexEntry existingEntry;
        readProductEntryAtRow(session, currentRow, &existingEntry);
        if (strcmp(existingEntry.productName, newRecord->productName) > 0) {
            break;
        }
        previousRow = currentRow;
        currentRow  = existingEntry.nextProductRowOffset;
    }

    if (previousRow == END_OF_LIST_SENTINEL) {
        /* New product becomes the alphabetical head. */
        newEntry.nextProductRowOffset = cityEntry.firstProductRowOffset;
        writeProductEntryAtRow(session, newRow, &newEntry);
        cityEntry.firstProductRowOffset = newRow;
        writeCityEntryAtRow(session, cityRow, &cityEntry);
        if (printTrace) {
            printf("  Spliced as new head: city's firstProductRowOffset is now %ld; "
                   "new row's next is %ld\n",
                   newRow, newEntry.nextProductRowOffset);
        }
    } else {
        ProductIndexEntry predecessorEntry;
        readProductEntryAtRow(session, previousRow, &predecessorEntry);
        newEntry.nextProductRowOffset = predecessorEntry.nextProductRowOffset;
        writeProductEntryAtRow(session, newRow, &newEntry);
        predecessorEntry.nextProductRowOffset = newRow;
        writeProductEntryAtRow(session, previousRow, &predecessorEntry);
        if (printTrace) {
            printf("  Spliced after row %ld: predecessor's next is now %ld; "
                   "new row's next is %ld\n",
                   previousRow, newRow, newEntry.nextProductRowOffset);
        }
    }

    return INSERT_OK;
}

/* ===========================================================================
 * Section 7 - Interactive console UI
 *
 * Continuous menu loop. Every operation goes through the IndexAccessSession
 * so the data file is never linearly scanned on the search path.
 * ======================================================================== */

/* Read a line of input from stdin into the given buffer, stripping the
 * trailing newline. Returns true on success, false on EOF. */
static bool readUserInputLine(char *buffer, size_t bufferCapacity) {
    if (fgets(buffer, (int)bufferCapacity, stdin) == NULL) return false;
    size_t length = strlen(buffer);
    while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r')) {
        buffer[--length] = '\0';
    }
    return true;
}

/* Build all four binary files from the source JSON dataset. */
static bool buildAllIndexesFromJson(const char *jsonInputPath) {
    ProductRecordList productList;
    productRecordListInitialize(&productList);
    if (!loadProductRecordsFromJsonFile(jsonInputPath, &productList)) {
        productRecordListRelease(&productList);
        return false;
    }
    printf("Loaded %zu product records from '%s'\n", productList.count, jsonInputPath);
    buildAndWriteCountryIndexFile(&productList, COUNTRY_INDEX_FILE_PATH);
    buildAndWriteCityIndexFile(&productList,
                               COUNTRY_INDEX_FILE_PATH, CITY_INDEX_FILE_PATH);
    buildAndWriteProductIndexAndDataFile(&productList,
                                         CITY_INDEX_FILE_PATH,
                                         PRODUCT_INDEX_FILE_PATH,
                                         DATA_FILE_PATH);
    productRecordListRelease(&productList);
    printf("All indexes built: %s, %s, %s, %s\n",
           COUNTRY_INDEX_FILE_PATH, CITY_INDEX_FILE_PATH,
           PRODUCT_INDEX_FILE_PATH, DATA_FILE_PATH);
    return true;
}

/* Demonstrate Replacement Selection Sort with full tracing on the
 * country list extracted from the source JSON. */
static void demonstrateReplacementSelectionSortOnCountries(const char *jsonInputPath) {
    ProductRecordList productList;
    productRecordListInitialize(&productList);
    if (!loadProductRecordsFromJsonFile(jsonInputPath, &productList)) {
        productRecordListRelease(&productList);
        return;
    }
    char **countryNamesInEncounterOrder = calloc(productList.count, sizeof(char *));
    for (size_t recordIndex = 0; recordIndex < productList.count; recordIndex++) {
        countryNamesInEncounterOrder[recordIndex] =
            (char *)productList.items[recordIndex].countryName;
    }
    size_t distinctCount = 0;
    char **distinctNames = extractDistinctStringsInEncounterOrder(
        countryNamesInEncounterOrder, productList.count, &distinctCount);
    free(countryNamesInEncounterOrder);

    printf("Source order (distinct country names as they appear in the JSON):\n");
    for (size_t printIndex = 0; printIndex < distinctCount; printIndex++) {
        printf("  [%zu] %s\n", printIndex + 1, distinctNames[printIndex]);
    }

    StringRunCollection runs;
    replacementSelectionSortStrings(distinctNames, distinctCount,
                                    REPLACEMENT_SELECTION_HEAP_SIZE, true, &runs);

    printf("\nRuns produced:\n");
    for (size_t runIndex = 0; runIndex < runs.runCount; runIndex++) {
        printf("  run %zu: ", runIndex + 1);
        for (size_t itemIndex = 0; itemIndex < runs.runs[runIndex].count; itemIndex++) {
            if (itemIndex > 0) printf(", ");
            printf("%s", runs.runs[runIndex].strings[itemIndex]);
        }
        printf("\n");
    }

    char **mergedSorted    = NULL;
    size_t mergedCount     = 0;
    mergeStringRunsIntoSortedArray(&runs, &mergedSorted, &mergedCount);
    printf("\nFinal sorted output after k-way merge:\n");
    for (size_t mergedIndex = 0; mergedIndex < mergedCount; mergedIndex++) {
        printf("  [%zu] %s\n", mergedIndex + 1, mergedSorted[mergedIndex]);
    }
    free(mergedSorted);
    releaseStringRunCollection(&runs, false);
    freeStringArray(distinctNames, distinctCount);
    productRecordListRelease(&productList);
}

/* Embedded self-tests (a condensed mirror of tests.c) so the grader can
 * verify correctness from the submitted file alone. */
static int runEmbeddedSelfTests(IndexAccessSession *session) {
    int passed = 0, total = 0;
    #define EMBED_CHECK(label, condition)                                    \
        do {                                                                  \
            total += 1;                                                       \
            bool conditionResult = (condition);                              \
            printf("  %s : %s\n", conditionResult ? "PASS" : "FAIL", label); \
            if (conditionResult) passed += 1;                                \
        } while (0)

    EMBED_CHECK("country index has 10 entries",  session->countryEntryCount == 10);
    EMBED_CHECK("city index has 80 entries",     session->cityEntryCount    == 80);
    EMBED_CHECK("product index has >= 800 entries", session->productEntryCount >= 800);

    /* Verify Country Index is alphabetically sorted. */
    {
        bool ascending = true;
        char previousName[MAX_COUNTRY_NAME_LENGTH] = "";
        for (long row = 1; (size_t)row <= session->countryEntryCount; row++) {
            CountryIndexEntry entry;
            readCountryEntryAtRow(session, row, &entry);
            if (row > 1 && strcmp(previousName, entry.countryName) > 0) ascending = false;
            copyStringIntoFixedBuffer(previousName, sizeof(previousName), entry.countryName);
        }
        EMBED_CHECK("country index is alphabetically sorted", ascending);
    }

    /* Verify every city's product chain is alphabetical. */
    {
        bool allAlphabetical = true;
        for (long cityRow = 1; (size_t)cityRow <= session->cityEntryCount; cityRow++) {
            CityIndexEntry cityEntry;
            readCityEntryAtRow(session, cityRow, &cityEntry);
            long currentRow = cityEntry.firstProductRowOffset;
            char previousName[MAX_PRODUCT_NAME_LENGTH] = "";
            bool firstStep = true;
            while (currentRow != END_OF_LIST_SENTINEL) {
                ProductIndexEntry productEntry;
                readProductEntryAtRow(session, currentRow, &productEntry);
                if (!firstStep && strcmp(previousName, productEntry.productName) > 0) {
                    allAlphabetical = false;
                    break;
                }
                copyStringIntoFixedBuffer(previousName, sizeof(previousName),
                                          productEntry.productName);
                firstStep   = false;
                currentRow  = productEntry.nextProductRowOffset;
            }
        }
        EMBED_CHECK("every city's product chain is alphabetical", allAlphabetical);
    }

    /* Verify product index entries point at the matching data records. */
    {
        bool allMatch = true;
        size_t toCheck = session->productEntryCount < 50 ? session->productEntryCount : 50;
        for (long productRow = 1; (size_t)productRow <= toCheck; productRow++) {
            ProductIndexEntry productEntry;
            readProductEntryAtRow(session, productRow, &productEntry);
            ProductRecord record;
            readDataRecordAtByteOffset(session, productEntry.dataFileRecordOffset, &record);
            if (strcmp(record.productName,    productEntry.productName)    != 0 ||
                strcmp(record.cityName,       productEntry.owningCityName) != 0) {
                allMatch = false;
                break;
            }
        }
        EMBED_CHECK("product index entries reference correct .dat records", allMatch);
    }

    #undef EMBED_CHECK
    printf("\n  %d / %d embedded self-tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}

/* Print the menu and read one option character. Returns the chosen digit
 * or -1 on EOF. */
static int promptForMenuChoice(void) {
    printf("\n==================== Multi-Level Index ====================\n");
    printf("[1] Search by country\n");
    printf("[2] Search by city\n");
    printf("[3] Search by product\n");
    printf("[4] Display Country Index (sorted)\n");
    printf("[5] Display City Index (alphabetical traversal per country)\n");
    printf("[6] Display Product Index (alphabetical traversal per city)\n");
    printf("[7] Insert a new product\n");
    printf("[8] Apply Replacement Selection Sort step-by-step (Country Index)\n");
    printf("[9] Run self-tests\n");
    printf("[0] Exit\n");
    printf("===========================================================\n");
    printf("Choose an option: ");
    fflush(stdout);

    char inputBuffer[32];
    if (!readUserInputLine(inputBuffer, sizeof(inputBuffer))) return -1;
    int choice = -1;
    if (sscanf(inputBuffer, "%d", &choice) != 1) return -2;
    return choice;
}

static void runMenuOptionSearchByCountry(IndexAccessSession *session) {
    char buffer[MAX_COUNTRY_NAME_LENGTH];
    printf("Enter country name: ");
    fflush(stdout);
    if (!readUserInputLine(buffer, sizeof(buffer))) return;
    searchAndPrintByCountry(session, buffer, true);
}

static void runMenuOptionSearchByCity(IndexAccessSession *session) {
    char buffer[MAX_CITY_NAME_LENGTH];
    printf("Enter city name: ");
    fflush(stdout);
    if (!readUserInputLine(buffer, sizeof(buffer))) return;
    searchAndPrintByCity(session, buffer, true);
}

static void runMenuOptionSearchByProduct(IndexAccessSession *session) {
    char buffer[MAX_PRODUCT_NAME_LENGTH];
    printf("Enter exact product name: ");
    fflush(stdout);
    if (!readUserInputLine(buffer, sizeof(buffer))) return;
    searchAndPrintByProductName(session, buffer, true);
}

static void runMenuOptionInsertProduct(IndexAccessSession *session) {
    ProductRecord newRecord;
    memset(&newRecord, 0, sizeof(newRecord));
    char inputBuffer[256];

    printf("Enter country name : "); fflush(stdout);
    if (!readUserInputLine(inputBuffer, sizeof(inputBuffer))) return;
    copyStringIntoFixedBuffer(newRecord.countryName, sizeof(newRecord.countryName), inputBuffer);

    printf("Enter city name    : "); fflush(stdout);
    if (!readUserInputLine(inputBuffer, sizeof(inputBuffer))) return;
    copyStringIntoFixedBuffer(newRecord.cityName, sizeof(newRecord.cityName), inputBuffer);

    printf("Enter product name : "); fflush(stdout);
    if (!readUserInputLine(inputBuffer, sizeof(inputBuffer))) return;
    copyStringIntoFixedBuffer(newRecord.productName, sizeof(newRecord.productName), inputBuffer);

    printf("Enter brand        : "); fflush(stdout);
    if (!readUserInputLine(inputBuffer, sizeof(inputBuffer))) return;
    copyStringIntoFixedBuffer(newRecord.productBrand, sizeof(newRecord.productBrand), inputBuffer);

    printf("Enter category     : "); fflush(stdout);
    if (!readUserInputLine(inputBuffer, sizeof(inputBuffer))) return;
    copyStringIntoFixedBuffer(newRecord.productCategory, sizeof(newRecord.productCategory),
                              inputBuffer);

    printf("Enter price (int)  : "); fflush(stdout);
    if (!readUserInputLine(inputBuffer, sizeof(inputBuffer))) return;
    sscanf(inputBuffer, "%d", &newRecord.priceAmount);

    printf("Enter currency     : "); fflush(stdout);
    if (!readUserInputLine(inputBuffer, sizeof(inputBuffer))) return;
    copyStringIntoFixedBuffer(newRecord.priceCurrency, sizeof(newRecord.priceCurrency),
                              inputBuffer);

    printf("Enter stock (int)  : "); fflush(stdout);
    if (!readUserInputLine(inputBuffer, sizeof(inputBuffer))) return;
    sscanf(inputBuffer, "%d", &newRecord.inventoryStock);

    /* Synthesise a product identifier from the country and city codes. */
    snprintf(newRecord.productIdentifier, sizeof(newRecord.productIdentifier),
             "MAN-%.3s-%05d",
             newRecord.cityName,
             (int)(session->productEntryCount + 1));

    InsertProductOutcome outcome = insertProductIntoCity(session, &newRecord, true);
    switch (outcome) {
        case INSERT_OK:
            printf("Insert succeeded. Product index now has %zu rows.\n",
                   session->productEntryCount);
            break;
        case INSERT_CITY_NOT_FOUND:
            printf("No city named '%s' in country '%s'.\n",
                   newRecord.cityName, newRecord.countryName);
            break;
        case INSERT_DUPLICATE_NAME_IN_CITY:
            printf("A product named '%s' already exists in '%s'.\n",
                   newRecord.productName, newRecord.cityName);
            break;
    }
}

static void runInteractiveMenuLoop(IndexAccessSession *session, const char *jsonInputPath) {
    while (true) {
        int choice = promptForMenuChoice();
        if (choice == -1) {
            printf("\nEnd of input - exiting.\n");
            return;
        }
        switch (choice) {
            case 1:  runMenuOptionSearchByCountry(session); break;
            case 2:  runMenuOptionSearchByCity(session);    break;
            case 3:  runMenuOptionSearchByProduct(session); break;
            case 4:  displayCountryIndexSorted(session);    break;
            case 5:  displayCityIndexAlphabeticalPerCountry(session); break;
            case 6:  displayProductIndexAlphabeticalPerCity(session); break;
            case 7:  runMenuOptionInsertProduct(session);   break;
            case 8:  demonstrateReplacementSelectionSortOnCountries(jsonInputPath); break;
            case 9:  runEmbeddedSelfTests(session);         break;
            case 0:  printf("Goodbye.\n"); return;
            default: printf("Unknown option. Please choose a number 0-9.\n"); break;
        }
    }
}

#ifndef BUILDING_TESTS
int main(int argumentCount, char **argumentValues) {
    const char *jsonInputPath = (argumentCount >= 2)
                                ? argumentValues[1]
                                : DEFAULT_JSON_INPUT_PATH;

    if (!buildAllIndexesFromJson(jsonInputPath)) {
        return EXIT_FAILURE;
    }

    IndexAccessSession session;
    if (!openIndexAccessSession(&session, COUNTRY_INDEX_FILE_PATH, CITY_INDEX_FILE_PATH,
                                PRODUCT_INDEX_FILE_PATH, DATA_FILE_PATH)) {
        fprintf(stderr, "Failed to open one of the index files for read/write access.\n");
        closeIndexAccessSession(&session);
        return EXIT_FAILURE;
    }

    runInteractiveMenuLoop(&session, jsonInputPath);
    closeIndexAccessSession(&session);
    return EXIT_SUCCESS;
}
#endif /* BUILDING_TESTS */
