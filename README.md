# Logpress: Distributed Template-Based Log Compressor & Searcher 🚀

Owner of this branch - pjibhak [Pranav Jibhakate](https://github.com/pranavJibhakate)

**Logpress** is a distributed system for efficient log compression, decompression, and search using a template-based mechanism and custom zlib dictionaries. It now includes microservices for chunk-based log transport and processing.

---

## Features ✨

- **Compress:** Deduplicate and compress log files using templates.
- **Decompress:** Reconstruct logs from compressed archives.
- **Search:** Search compressed archives without full decompression.
- **Distributed Design:** Leverages microservices (`chunk_sender`, `chunk_receiver`, `dbserver`) for scalable log ingestion and processing.
- **Zlib with Custom Dictionary:** Improves compression ratio by reusing common log patterns.
- **SQLite Metadata:** Keeps track of templates, tokens, and filenames for accurate log reconstruction.

---

## Project Structure 🗂️

<pre>
.
├── chunk_reciever/         # Receives compressed log chunks (Go + Docker)
│   ├── chunk_reciever.go
│   └── Dockerfile
├── chunk_sender/           # Sends log chunks to receiver
│   ├── chunk_sender.go
│   ├── Dockerfile
│   ├── go.mod
│   ├── HDFS2.log
│   └── settings.json
├── dbserver/               # Manages metadata and coordination (Go)
│   ├── dbserver.go
│   ├── Dockerfile
│   ├── go.mod
│   └── go.sum
├── external/               # External headers
│   └── json.hpp
├── main.cpp
├── compressor.cpp / .hpp
├── decompressor.cpp / .hpp
├── searcher.cpp / .hpp
├── sqlite_helper.cpp / .hpp
├── Makefile
├── docker-compose.yml
└── README.md
</pre>

---

## Prerequisites ✅

- C++17 compiler (e.g., `g++`, `clang++`)
- [zlib](https://zlib.net/) (`-lz`)
- [SQLite3](https://sqlite.org/index.html) (`-lsqlite3`)
- Go 1.22
- Docker & Docker Compose

---

## Building the Core C++ Components 🛠️

### Using Makefile

```bash
make
```

## Running the Distributed System 🧱

### Start All Services

```bash
docker-compose up --build
```

This launches:

- chunk_sender: Reads and sends logs

- chunk_reciever: Receives and compresses them

- dbserver: Stores template and metadata info

### Configuration

Edit chunk_sender/settings.json to specify the links of the worker nodes.

The existing settings.json works with the example docker-compose.yml.

## Running the System with Docker 🐳

Follow these steps to use the distributed Logpress system via Docker:

---

### 1. Start All Services

From the root folder of the project, run:

```bash
sudo docker-compose up --build
```

This command will build and start the following services:

- `chunk_sender`
- `chunk_reciever`
- `dbserver`

---

### 2. Connect to the `chunk_sender` Container

Open another terminal and follow these steps:

#### 2.1 Get the Docker Network Name

List all Docker networks:

```bash
sudo docker network ls
```

Look for the network that was just created. It typically has a name based on your project folder.

#### 2.2 Run `chunk_sender` Interactively

Replace `<NETWORK>` with the actual network name found above:

```bash
sudo docker run -it --rm --network <NETWORK> --entrypoint /bin/sh chunk_sender
```

This opens an interactive shell inside the `chunk_sender` container.

---

### 3. Run Compression and Decompression Commands

Inside the `chunk_sender` shell:

#### 3.1 Compress a File

To compress a file:

```bash
./chunk_sender compress <filename>
```

- This command reads the specified `<filename>`, compresses it using the template-based method, and sends it to the `chunk_reciever` service.

#### 3.2 Decompress Files

To decompress the previously compressed files:

```bash
./chunk_sender decompress
```

- The decompressed files will be saved inside the `./decompressed` folder within the `chunk_sender` container.

---

### Example Workflow

1. Start all services:

   ```bash
   sudo docker-compose up --build
   ```

2. In a new terminal, get the Docker network name:

   ```bash
   sudo docker network ls
   ```

3. Connect to `chunk_sender`:

   ```bash
   sudo docker run -it --rm --network <NETWORK> --entrypoint /bin/sh chunk_sender
   ```

4. Inside the container shell:

   - Compress a log file:

     ```bash
     ./chunk_sender compress ./HDFS2.log
     ```

   - Decompress logs:

     ```bash
     ./chunk_sender decompress
     ```

5. Decompressed logs will appear inside the `./decompressed` directory.

6. Search log functionality is not implemented yet.

---
