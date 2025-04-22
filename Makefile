# Makefile for Logpress2

# --- Tools & flags ---
GOCMD        := go
GOBUILD      := $(GOCMD) build

CXX          := g++
CXXFLAGS     := -g
LDFLAGS      := -lz -lsqlite3 -lcurl

# --- Directories ---
DBSERVER_DIR := dbserver
RECEIVER_DIR := chunk_reciever
SENDER_DIR   := chunk_sender

# --- C++ sources & target ---
CPP_SRCS     := main.cpp compressor.cpp decompressor.cpp searcher.cpp sqlite_helper.cpp
CPP_TARGET   := logpress

# --- Default target ---
.PHONY: all
all: dbserver chunk_reciever chunk_sender $(CPP_TARGET)

# --- Go builds ---
.PHONY: dbserver
dbserver:
	cd $(DBSERVER_DIR) && $(GOBUILD) dbserver.go

.PHONY: chunk_reciever
chunk_reciever:
	cd $(RECEIVER_DIR) && $(GOBUILD) chunk_reciever.go

.PHONY: chunk_sender
chunk_sender:
	cd $(SENDER_DIR) && $(GOBUILD) chunk_sender.go

# --- C++ build + copy into receiver ---
$(CPP_TARGET): $(CPP_SRCS)
	$(CXX) $(CXXFLAGS) -o $(CPP_TARGET) $(CPP_SRCS) $(LDFLAGS)
	cp $(CPP_TARGET) $(RECEIVER_DIR)/$(CPP_TARGET)

# --- Clean up all binaries ---
.PHONY: clean
clean:
	rm -f \
	  $(DBSERVER_DIR)/dbserver \
	  $(RECEIVER_DIR)/chunk_reciever \
	  $(RECEIVER_DIR)/$(CPP_TARGET) \
	  $(SENDER_DIR)/chunk_sender \
	  $(CPP_TARGET)
