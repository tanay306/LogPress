# Template-Based Log Compressor & Searcher

This project provides a command-line utility to compress log files using a template-based approach combined with zlib compression. It supports three main commands:

- **compress**: Compress one or more log files into a single archive.
- **decompress**: Decompress an archive and reconstruct the original log files.
- **search**: Search within the compressed archive for log lines matching a given search term.

The archive format starts with the magic `"TMZL"`, followed by the uncompressed size, compressed size, and the zlib-compressed data. The internal uncompressed data begins with `"TMPL"` and includes the dictionary of templates, log lines, and filenames.

## Prerequisites

- A C++ compiler with C++17 support (e.g., g++ or clang++).
- [zlib](https://zlib.net/) development library installed (link with `-lz`).

## Building the Project

To compile the project, use a command similar to the following. Adjust the source file names as necessary if your project is split into multiple files (e.g., `compressor.cpp`, `decompressor.cpp`, `searcher.cpp`, and `main.cpp`):

```bash
g++ -std=c++17 -O2 -lz main.cpp -o logpress
```
Make sure that the include paths for zlib and the standard library are set correctly if they’re not in your default locations.

## Usage

Once compiled, the executable (here called `logpress`) supports the following commands:

### Compressing Files

To compress one or more log files into an archive:

```bash
./logpress compress <archive> <file1> [file2 ...]
```

Example:

```bash
./logpress compress archive.tmzl log1.txt log2.txt
```

This command reads `log1.txt` and `log2.txt`, compresses them using the template-based approach combined with zlib, and creates an archive file named `archive.tmzl`.

### Decompressing an Archive

To decompress an archive and reconstruct the original log files into an output folder:

```bash
./logpress decompress <archive> <output_folder>
```

Example:

```bash
./logpress decompress archive.tmzl output_logs
```

This command extracts the compressed data from `archive.tmzl` and writes the reconstructed log files to the directory `output_logs`.

### Searching Within an Archive

To search for a specific term in the archive without decompressing all files:

```bash
./logpress search <archive> <search_term>
```

Example:

```bash
./logpress search archive.tmzl "ERROR"
```

This command searches the archive for any log lines containing the term "ERROR" and prints out the matching lines along with the corresponding filename.