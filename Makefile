# Build glue for Homework #2 - Multi-Level Indexing.
# Targets:
#   make            -> build the submission program (./2020510001)
#   make test       -> build and run the development tests
#   make run        -> build and run the program against ./Assignment\ -2.json
#   make clean      -> remove generated files

CC            := clang
WARNING_FLAGS := -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter
JSON_C_FLAGS  := $(shell pkg-config --cflags json-c)
JSON_C_LIBS   := $(shell pkg-config --libs json-c)
CFLAGS        := -std=c11 $(WARNING_FLAGS) -O0 -g $(JSON_C_FLAGS)
LDFLAGS       := $(JSON_C_LIBS)

SUBMISSION_BINARY := 2020510001
SUBMISSION_SOURCE := 2020510001.c
TEST_BINARY       := tests
TEST_SOURCE       := tests.c
DATA_FILES        := products.dat country_index.dat city_index.dat product_index.dat

.PHONY: all test run clean

all: $(SUBMISSION_BINARY)

$(SUBMISSION_BINARY): $(SUBMISSION_SOURCE)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(TEST_BINARY): $(TEST_SOURCE) $(SUBMISSION_SOURCE)
	$(CC) $(CFLAGS) -DBUILDING_TESTS -o $@ $< $(LDFLAGS)

test: $(TEST_BINARY)
	./$(TEST_BINARY)

run: $(SUBMISSION_BINARY)
	./$(SUBMISSION_BINARY) "Assignment -2.json"

clean:
	rm -f $(SUBMISSION_BINARY) $(TEST_BINARY) $(DATA_FILES)
