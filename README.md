# Clustering Based Log Compressor & Searcher

Owner of this branch - tgandhi [Tanay Gandhi](https://github.com/tanay306)

- This branch is an intermediary testing branch for Chunk-based compression algorithm. The final branch with the latest code can be found here. [parallel-search](https://github.com/tanay306/LogPress/tree/parallel-search)

- We are keeping all the test branch to show contribution.

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

## Install Dependencies

To install all the dependecies with respect to clusterin run the following command

```bash
pip install pandas scikit-learn
```

## Clustering

In order to cluster the log enteries run this command

```bash
python3 clustering.py
```
This will create a file called clustered_logs.txt which contains the log entries in a clustered format.

## Usage

Once compiled, the executable (here called `logpress`) supports the following commands:

### Compressing Files

To compress one or more log files into an archive:

```bash
./logpress compress <archive> python3 clustering.py
```

Example:

```bash
./logpress compress compressed_archive.mylp python3 clustering.py
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
