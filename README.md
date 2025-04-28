# Logpress: Template-Based Log Compressor & Searcher 🚀

Owner of this branch - ndudhel [Neel Dudheliya](https://github.com/Neel317)

**Logpress**  is a command-line utility designed to efficiently compress, decompress, and search log files using a template-based approach. It leverages zlib (with a custom dictionary) to achieve high compression rates by deduplicating common log patterns.

## Features ✨

- **Compress:** Bundle one or more log files into a single archive with real-time progress tracking.
- **Decompress:** Reconstruct original log files from a compressed archive.
- **Search:** Quickly search through compressed logs using multithreaded processing—without fully decompressing the archive.
- **High Performance:** Utilize multiple CPU cores for faster searching while maintaining log order.
- **Visual Feedback:** Track compression progress with an intuitive progress bar.

## Archive Format 🗃️

An archive created by Logpress consists of:
- A file header starting with the magic string "TCDZ"
- A SQLite database (db/<archive_name>.meta.db) that stores templates, variables, and filenames. This is created inside the build folder or in the same place as your executable.
- Multiple compressed blocks containing the actual log data, compressed using zlib with a custom dictionary.

## Prerequisites ✅

- A C++ compiler with C++17 support (e.g., **g++** or **clang++**).
- [zlib](https://zlib.net/) development library (ensure you link with `-lz`).
- [SQLite3](https://www.sqlite.org/) development library.
- *(Optional)* [CMake](https://cmake.org/) for generating the build configuration.

## Building the Project 🛠️

### Using g++ Directly

```bash
g++ -std=c++17 -O2 -lz main.cpp compressor.cpp decompressor.cpp searcher.cpp sqlite_helper.cpp -o logpress
```

*Adjust the source file names if your project structure differs.*

### Using CMake (Recommended)

1. **Generate Build System (output goes to `./build`):**

    ```bash
    cmake -S . -B build
    ```
    OR
    ```bash
    cd build
    cmake ..
    ```
    if the previous command doesn't work for you.

2. **Build the Project:**

   ```bash
   cmake --build build
   ```
   If you are in build directory do:
   ```bash
   cmake --build .
   ```

3. **Run the Executable:**

   ```bash
   ./build/logpress
   ```
   If you are in build directory do:
   ```bash
   ./logpress
   ```

## Usage 📖

Once compiled, the executable (named **logpress**) supports the following commands:

### 1. Compressing Files

Compress one or more log files into a single archive. <archive> can be a complete path with folders as well. We have used it as ./archives/<name_of_archive> during the project.

```bash
./logpress compress <archive> <file1> [file2 ...]
```

**Example:**

```bash
./logpress compress archive.lgp log1.txt log2.txt
```

> This reads log1.txt and log2.txt, compresses them using a template-based approach combined with zlib compression, and produces an archive called archive.lgp. A progress bar displays compression status and speed in real time.

### 2. Decompressing an Archive

Reconstruct the original log files from an archive into a specified output folder.

```bash
./logpress decompress <archive> <output_folder>
```

**Example:**

```bash
./logpress decompress archive.lgp output_logs
```

> This extracts the compressed data from `archive.lgp` and writes the reconstructed logs into the `output_logs` directory.

### 3. Searching Within an Archive

Search for log lines matching a specific term within an archive without needing to decompress all files.

```bash
./logpress search <archive> <search_term>
```

**Example:**

```bash
./logpress search archive.lgp "ERROR"
```

> This command searches the archive using multiple threads for any log lines containing "ERROR" and prints the matching lines with highlighted search terms. Results maintain the original log order.

**Advanced Search**

The search function supports wildcard patterns with * and ?:

```bash
./logpress search archive.lgp "connection*failed"
```

```bash
./logpress search archive.lgp "10.??.23.120"
```

## Additional Notes 📝

- **Emojis & Visual Clarity:**  
  Emojis are used throughout the README to enhance readability and make the user guide more engaging.
- **Project Structure:**  
  Core source files include:
  - `compressor.cpp`
  - `decompressor.cpp`
  - `searcher.cpp`
  - `sqlite_helper.cpp`
  - `main.cpp`
- **Troubleshooting:**  
  - Ensure `zlib` is correctly installed and linked (use `-lz`).
  - Xcode SDK Path Issues:
  If you encounter SDK path errors after updating Xcode, try:
  - For sqlite issues, if you're using Homebrew on macOS, you may need to adjust your PATH or force-link sqlite:
    ```bash
    brew link sqlite --force
    ```
    or add:
    ```bash
    export PATH="/opt/homebrew/opt/sqlite/bin:$PATH"
    ```
    to your `~/.zshrc`.

Enjoy using **Logpress** to effectively manage and search through your logs! 🎉