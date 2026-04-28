# Build and Run Instructions

This program is a single C source file (`2020510001.c`) that implements the
multi-level secondary indexing system described in Homework #2. It depends
only on the standard C library and the system `json-c` library.

The repository ships with the input dataset (`Assignment -2.json`); the
program reads it at startup.

## 1. Install the `json-c` library

Pick the line that matches your operating system.

```sh
# macOS (Homebrew)
brew install json-c

# Debian / Ubuntu Linux
sudo apt-get install libjson-c-dev

# Fedora / RHEL Linux
sudo dnf install json-c-devel
```

## 2. Compile

```sh
clang -std=c11 -Wall -Wextra $(pkg-config --cflags json-c) \
      -o 2020510001 2020510001.c $(pkg-config --libs json-c)
```

`gcc` works the same way; just replace `clang` with `gcc`.

If `pkg-config` is unavailable, pass the include and library paths
explicitly. On macOS with Homebrew the paths are typically:

```sh
clang -std=c11 -Wall -Wextra \
      -I/opt/homebrew/include -L/opt/homebrew/lib \
      -o 2020510001 2020510001.c -ljson-c
```

## 3. Run

```sh
./2020510001
```

By default the program reads `Assignment -2.json` from the current
directory. To use a different path, pass it as the first argument:

```sh
./2020510001 path/to/your-dataset.json
```

## 4. Use the interactive menu

After loading the dataset, the program prints a menu and waits for input:

```
==================== Multi-Level Index ====================
[1] Search by country
[2] Search by city
[3] Search by product
[4] Display Country Index (sorted)
[5] Display City Index (alphabetical traversal per country)
[6] Display Product Index (alphabetical traversal per city)
[7] Insert a new product
[8] Apply Replacement Selection Sort step-by-step (Country Index)
[9] Run self-tests
[0] Exit
===========================================================
```

Type the option number and press Enter. The program loops until you
choose `[0] Exit`.

The recommended order for verifying correctness is:

1. `[4]` to confirm the Country Index is alphabetically sorted.
2. `[5]` to confirm each country's city chain is alphabetical.
3. `[6]` to confirm each city's product chain is alphabetical.
4. `[1]` then enter `Germany` to verify a search returns 80 products.
5. `[2]` then enter `Berlin` to verify a search returns 10 products.
6. `[3]` then enter a product name (for example `MacBook Air M3`) to see
   index-only traversal across cities.
7. `[7]` to insert a new product into a city and confirm the chain is
   updated without moving any existing record.
8. `[8]` to watch Replacement Selection Sort execute step by step on the
   country list, with run boundaries and the final k-way merge.
9. `[9]` to run the embedded self-tests and verify all pass.
10. `[0]` to exit cleanly.

## 5. Files generated at runtime

The program writes four binary files into the current directory:

| File                | Contents                              |
|---------------------|---------------------------------------|
| `country_index.dat` | Level 1 — sorted Country Index        |
| `city_index.dat`    | Level 2 — City Index with next chain  |
| `product_index.dat` | Level 3 — Product Index with chain    |
| `products.dat`      | Level 4 — fixed-size product records  |

These are recreated from the JSON dataset on every run, so they are safe
to delete between runs.
