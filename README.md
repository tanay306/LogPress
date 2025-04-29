# This is a Test feature branch

Owner of this branch - pjibhak [Pranav Jibhakate](https://github.com/pranavJibhakate)

- This branch is an intermediary testing branch for Distributed compression algorithm. The final branch with the latest code can be found here. [distributed-with-docker](https://github.com/tanay306/LogPress/tree/distributed-with-docker)

- We are keeping all the test branch to show contribution.

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
g++ -std=c++17 -o logpress main.cpp compressor.cpp decompressor.cpp searcher.cpp -lz
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
./logpress compress compressed_archive.mylp fileA.log  
```

This command reads `fileA.log`, compresses them using the template-based approach combined with zlib, and creates an archive file named `compressed_archive.mylp`.

### Decompressing an Archive

To decompress an archive and reconstruct the original log files into an output folder:

```bash
./logpress decompress <archive> <output_folder>
```

Example:

```bash
./logpress decompress compressed_archive.mylp Decomp
```

This command extracts the compressed data from `compressed_archive.mylp` and writes the reconstructed log files to the directory `Decomp`.

### Searching Within an Archive

To search for a specific term in the archive without decompressing all files:

```bash
./logpress search <archive> <search_term>
```

Example:

```bash
./logpress search compressed_archive.mylp Verification  
```

This command searches the archive for any log lines containing the term "Verification" and prints out the matching lines along with the corresponding filename.
