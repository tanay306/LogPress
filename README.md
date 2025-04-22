# Logpress: Template-Based Log Compressor & Searcher 🚀

**Logpress** is a command-line utility designed to efficiently compress, decompress, and search log files using a template-based approach. It leverages zlib (with a custom dictionary) to achieve high compression rates by deduplicating common log patterns.

## Features ✨

- **Compress:** Bundle one or more log files into a single archive.
- **Decompress:** Reconstruct original log files from a compressed archive.
- **Search:** Quickly search through compressed logs for specific terms—without fully decompressing the archive.

## Archive Format 🗃️

An archive created by Logpress consists of:

- A file header starting with the magic string `"TMZL"`.
- A global metadata section (prefixed with `"TMPL"`) that includes a dictionary of templates, variables, and filenames.
- One or more compressed blocks containing the actual log data, compressed using zlib with a custom dictionary.

## Prerequisites ✅

- A C++ compiler with C++17 support (e.g., **g++** or **clang++**).
- [zlib](https://zlib.net/) development library (ensure you link with `-lz`).
- _(Optional)_ [CMake](https://cmake.org/) for generating the build configuration.

## Building the Project 🛠️

### Using g++ Directly

```bash
g++ -g -o logpress main.cpp compressor.cpp decompressor.cpp searcher.cpp sqlite_helper.cpp -lz -lsqlite3
```

_Adjust the source file names if your project structure differs._

### Using CMake

1. **Generate Build System (output goes to `./build`):**

   ```bash
   cmake -S . -B build
   ```

2. **Build the Project:**

   ```bash
   cmake --build build
   ```

3. **Run the Executable:**

   ```bash
   ./build/logpress
   ```

## Usage 📖

Once compiled, the executable (named **logpress**) supports the following commands:

### 1. Compressing Files

Compress one or more log files into a single archive.

```bash
./logpress compress <archive> <file1> [file2 ...]
```

**Example:**

```bash
./logpress compress archive.tmzl log1.txt log2.txt
```

> This reads `log1.txt` and `log2.txt`, compresses them using a template-based approach combined with zlib compression, and produces an archive called `archive.tmzl`.

### 2. Decompressing an Archive

Reconstruct the original log files from an archive into a specified output folder.

```bash
./logpress decompress <archive> <output_folder>
```

**Example:**

```bash
./logpress decompress archive.tmzl output_logs
```

> This extracts the compressed data from `archive.tmzl` and writes the reconstructed logs into the `output_logs` directory.

### 3. Searching Within an Archive

Search for log lines matching a specific term within an archive without needing to decompress all files.

```bash
./logpress search <archive> <search_term>
```

**Example:**

```bash
./logpress search archive.tmzl "ERROR"
```

> This command searches the archive for any log lines containing the term `"ERROR"` and prints the matching lines along with their corresponding filenames.

## Internal Details 🔍

- **Template-Based Compression:**  
  Logpress deduplicates log entries by extracting and compressing common templates and numeric tokens.
- **Custom Zlib Dictionary:**  
  A custom dictionary is generated from templates, filenames, and variables. This dictionary is used during compression (and decompression) to improve compression ratios.
- **Metadata Storage:**  
  A SQLite database is used to store metadata such as templates, variable values, and file names. This helps in reconstructing the original logs accurately.
- **Regex-Based Classification:**  
  The project includes several regex patterns for classifying numeric tokens (e.g., IP addresses and timestamps), which are used during the template creation process.

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

For building use

sudo docker-compose up --build

To run the compressor
sudo docker run -it --rm --network logpress2_default --entrypoint /bin/sh chunk_sender
