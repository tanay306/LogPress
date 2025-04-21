#include "searcher.hpp"
#include "sqlite_helper.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <regex>
#include <cstring>
#include <filesystem>
#include <zlib.h>
#include <chrono>   // for timing
#include <cstdint>  // for uint32_t
#include <thread>   // for multithreading
#include <mutex>    // for synchronization
#include <future>   // for async processing
#include <queue>    // for processing queue
#include <condition_variable> // for thread coordination

// Utility to read 4 bytes as uint32_t from file
static uint32_t read_u32(std::ifstream& in) {
    uint32_t v;
    in.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

// Utility to read 4 bytes as uint32_t from memory buffer
static uint32_t read_u32_mem(const char*& p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    p += 4;
    return v;
}

// Decompress a single block using zlib + dictionary
static bool zlib_decompress_block(const std::vector<char>& in_data,
                                  size_t uncompressed_size,
                                  std::vector<char>& out_data,
                                  const std::string& dict) {
    z_stream strm{};
    if (inflateInit2(&strm, 15) != Z_OK) {
        return false;
    }

    out_data.resize(uncompressed_size);
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in_data.data()));
    strm.avail_in = static_cast<uInt>(in_data.size());
    strm.next_out = reinterpret_cast<Bytef*>(out_data.data());
    strm.avail_out = static_cast<uInt>(out_data.size());

    int ret = inflate(&strm, Z_FINISH);
    if (ret == Z_NEED_DICT && !dict.empty()) {
        // If the block needs a dictionary, set it
        if (inflateSetDictionary(&strm, reinterpret_cast<const Bytef*>(dict.data()), dict.size()) != Z_OK) {
            inflateEnd(&strm);
            return false;
        }
        ret = inflate(&strm, Z_FINISH);
    }

    bool ok = (ret == Z_STREAM_END && strm.total_out == uncompressed_size);
    inflateEnd(&strm);
    return ok;
}

// A helper to highlight all literal occurrences of "search_term" in "line" using ANSI codes.
static std::string highlightLiteral(const std::string& line, const std::string& search_term) {
    if (search_term.empty()) return line;  // nothing to highlight

    // We'll do a simple repeated   find and replace with ANSI wrapping
    // \x1B[1;31m = bright red start, \x1B[0m = reset
    const std::string start_highlight = "\x1B[1;31m";
    const std::string end_highlight   = "\x1B[0m";

    std::string result;
    result.reserve(line.size() + 16); // small buffer overhead
    size_t pos = 0;
    while (true) {
        size_t found = line.find(search_term, pos);
        if (found == std::string::npos) {
            // no more occurrences
            result.append(line, pos);
            break;
        }
        // copy everything up to 'found'
        result.append(line, pos, found - pos);
        // insert highlight
        result.append(start_highlight);
        result.append(search_term);
        result.append(end_highlight);
        // move pos
        pos = found + search_term.size();
    }
    return result;
}

// Return a list of literal segments from a wildcard pattern
static std::vector<std::string> extractLiteralSegments(const std::string& pattern) {
    std::vector<std::string> segments;
    std::string buffer;

    for (char c : pattern) {
        if (c == '*' || c == '?') {
            // flush buffer
            if (!buffer.empty()) {
                segments.push_back(buffer);
                buffer.clear();
            }
            // skip the wildcard
        } else {
            // accumulate literal char
            buffer.push_back(c);
        }
    }
    // final flush
    if (!buffer.empty()) {
        segments.push_back(buffer);
    }
    return segments;
}

// 3) highlightAllSegments: apply highlightLiteral to each segment in turn
static std::string highlightAllSegments(std::string line,
    const std::vector<std::string>& segments) {
    for (auto &seg : segments) {
        line = highlightLiteral(line, seg);
    }
    return line;
}

// Structure to store search results while preserving order
struct BlockSearchResult {
    int block_id;
    std::vector<std::string> matched_lines;
    size_t lines_processed;
};

bool search_archive_template_zlib(const std::string& archive_path,
                                  const std::string& search_term) 
{
    using clock = std::chrono::steady_clock;

    auto search_start_time = clock::now();
    bool found_first_match = false;
    std::chrono::time_point<clock> first_match_time;

    // 1) Open the compressed archive file
    std::ifstream in(archive_path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "❌ Cannot open archive: " << archive_path << "\n";
        return false;
    }

    // 2) Verify magic
    char magic[4];
    in.read(magic, 4);
    if (std::strncmp(magic, "TCDZ", 4) != 0) {
        std::cerr << "❌ Invalid archive format.\n";
        return false;
    }

    // 3) Load metadata from .meta.db (templates, variables, types, filenames)
    sqlite3* db = nullptr;
    std::vector<std::string> templates, variables, filenames;

    std::filesystem::path archive_p(archive_path);
    std::filesystem::path meta_path = "./db/" + (archive_p.filename().string() + ".meta.db");
    std::cout << "📂 Opening meta.db at: " << meta_path << "\n";

    if (sqlite3_open(meta_path.string().c_str(), &db) != SQLITE_OK) {
        std::cerr << "❌ Failed to open meta.db at: " << meta_path << "\n";
        return false;
    }
    if (!load_templates_and_variables(db, templates, variables, filenames)) {
        std::cerr << "❌ Failed to load from meta.db\n";
        sqlite3_close(db);
        return false;
    }
    sqlite3_close(db);

    // Build zlib dictionary
    std::string dict;
    dict.reserve(templates.size() * 50 + variables.size() * 20 + filenames.size() * 10);
    for (auto& t : templates)  dict += t;
    for (auto& v : variables)  dict += v;
    for (auto& f : filenames)  dict += f;

    // 4) Prepare the search logic (regex or substring)
    std::regex search_regex;
    bool use_regex = false;
    std::string rough_substr; // optional substring to skip expensive regex

    if (!search_term.empty() &&
        (search_term.find('*') != std::string::npos || search_term.find('?') != std::string::npos)) 
    {
        // Convert wildcard * / ? to a basic regex
        std::string pattern;
        for (char c : search_term) {
            if      (c == '*')  pattern += ".*";
            else if (c == '?')  pattern += ".";
            else if (std::string(".^$\\[](){}+|").find(c) != std::string::npos) pattern += '\\', pattern += c;
            else    pattern += c;
        }
        search_regex = std::regex(pattern);
        use_regex = true;

        // If there's an initial literal portion (ex: "abc*"), keep it for quick substring check
        size_t starPos = search_term.find('*');
        if (starPos != std::string::npos && starPos > 0) {
            rough_substr = search_term.substr(0, starPos);
        }
    }

    std::vector<std::string> literalSegments;
    if (use_regex) {
        literalSegments = extractLiteralSegments(search_term);
    }

    // Create worker function that processes a single block
    auto process_block = [&](int block_index, const std::vector<char>& comp_buf, 
                           uint32_t lines, uint32_t uncomp) -> BlockSearchResult {
        BlockSearchResult result;
        result.block_id = block_index;
        result.lines_processed = lines;
        
        // Decompress block
        std::vector<char> block;
        if (!zlib_decompress_block(comp_buf, uncomp, block, dict)) {
            std::cerr << "❌ Decompression failed (block#" << block_index << ").\n";
            return result;
        }

        // Parse lines
        const char* p = block.data();
        size_t block_size = block.size();
        for (uint32_t line_idx = 0; line_idx < lines; line_idx++) {
            if ((p + 12) > (block.data() + block_size)) {
                std::cerr << "❌ Block data truncated reading line#" 
                          << line_idx << " of block#" << block_index << "\n";
                return result;
            }

            uint32_t file_id   = read_u32_mem(p); // SHIFT
            uint32_t tpl_id    = read_u32_mem(p);
            uint32_t var_count = read_u32_mem(p);

            // read var_ids
            std::vector<uint32_t> var_ids(var_count);
            for (uint32_t j = 0; j < var_count; j++) {
                if ((p + 4) > (block.data() + block_size)) {
                    std::cerr << "❌ Block data truncated while reading var_ids.\n";
                    return result;
                }
                var_ids[j] = read_u32_mem(p);
            }

            // validate tpl_id
            if (tpl_id >= templates.size()) {
                // out-of-range => skip line
                continue;
            }
            const std::string& tpl = templates[tpl_id];

            // Reconstruct line by substituting <VAR> placeholders
            std::string reconstructed;
            reconstructed.reserve(tpl.size() + var_count * 12);
            size_t last = 0, vi = 0;
            while (true) {
                size_t pos = tpl.find("<VAR>", last);
                if (pos == std::string::npos) {
                    reconstructed.append(tpl, last);
                    break;
                }
                reconstructed.append(tpl, last, pos - last);

                // variable substitution
                std::string val = "???";
                if (vi < var_ids.size()) {
                    uint32_t v_id = var_ids[vi];
                    if (v_id < variables.size()) {
                        val = variables[v_id];
                    }
                }
                reconstructed += val;
                last = pos + 5; // skip "<VAR>"
                vi++;
            }

            // Now run the text search
            bool match = false;
            if (search_term.empty()) {
                match = true;
            }
            else if (use_regex) {
                // optional substring pre-check
                if (!rough_substr.empty() && reconstructed.find(rough_substr) == std::string::npos) {
                    match = false;
                } else {
                    match = std::regex_search(reconstructed, search_regex);
                    // if matched, do partial highlight of literal segments
                    if (match) {
                        reconstructed = highlightAllSegments(reconstructed, literalSegments);
                    }
                }
            } else {
                // literal substring search => also highlight
                size_t found_pos = reconstructed.find(search_term);
                if (found_pos != std::string::npos) {
                    match = true;
                    // highlight all occurrences
                    reconstructed = highlightLiteral(reconstructed, search_term);
                }
            }

            // if matched => store
            if (match) {
                result.matched_lines.push_back(reconstructed);
            }
        }
        return result;
    };

    // Determine thread count (hardware_concurrency with a reasonable min/max)
    unsigned int thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) thread_count = 4;  // Default if detection fails
    // Limit max threads to avoid system overload
    thread_count = std::min(thread_count, 16u); 

    std::cout << "🧵 Using " << thread_count << " threads for search\n";

    // Create thread pool
    std::vector<std::future<BlockSearchResult>> futures;
    
    // Read blocks and submit tasks
    int block_index = 0;
    std::vector<BlockSearchResult> ordered_results;
    size_t total_lines_scanned = 0;
    size_t total_matches = 0;
    
    // Keep a limited number of active tasks to manage memory usage
    const size_t max_active_tasks = thread_count * 2;
    
    while (in.peek() != EOF) {
        // Read block header
        if (!in.good()) break; // safety
        uint32_t lines = read_u32(in);
        if (!in.good()) break;
        uint32_t uncomp = read_u32(in);
        if (!in.good()) break;
        uint32_t comp = read_u32(in);
        if (!in.good()) break;

        // Read compressed data
        std::vector<char> comp_buf(comp);
        in.read(comp_buf.data(), comp);
        if (in.gcount() < (std::streamsize)comp) {
            std::cerr << "❌ Truncated block data.\n";
            return false;
        }
        
        // Limit active tasks to avoid excessive memory usage
        if (futures.size() >= max_active_tasks) {
            // Wait for the earliest submitted task to complete
            auto result = futures.front().get();
            futures.erase(futures.begin());
            
            // Process result
            total_lines_scanned += result.lines_processed;
            total_matches += result.matched_lines.size();
            
            // Store for ordered output
            ordered_results.push_back(std::move(result));
            
            // Track first match time
            if (!found_first_match && !ordered_results.back().matched_lines.empty()) {
                found_first_match = true;
                first_match_time = clock::now();
            }
        }
        
        // Submit new task
        futures.push_back(
            std::async(std::launch::async, 
                      process_block, 
                      block_index++, 
                      comp_buf, 
                      lines, 
                      uncomp)
        );
    }
    
    // Wait for remaining tasks to complete
    for (auto& future : futures) {
        auto result = future.get();
        total_lines_scanned += result.lines_processed;
        total_matches += result.matched_lines.size();
        ordered_results.push_back(std::move(result));
        
        // Track first match time
        if (!found_first_match && !ordered_results.back().matched_lines.empty()) {
            found_first_match = true;
            first_match_time = clock::now();
        }
    }
    
    // Sort results by block_id to maintain original order
    std::sort(ordered_results.begin(), ordered_results.end(), 
              [](const BlockSearchResult& a, const BlockSearchResult& b) {
                  return a.block_id < b.block_id;
              });
    
    // Output matched lines in order
    for (const auto& result : ordered_results) {
        for (const auto& line : result.matched_lines) {
            std::cout << line << "\n";
        }
    }

    // Print statistics
    auto end_time = clock::now();
    double total_sec = std::chrono::duration<double>(end_time - search_start_time).count();
    std::cout << "\nScanned " << block_index << " blocks, " << total_lines_scanned << " lines.\n";
    std::cout << "Found " << total_matches << " matches.\n";

    if (found_first_match) {
        double first_match_sec = std::chrono::duration<double>(first_match_time - search_start_time).count();
        std::cout << "Time to first match: " << first_match_sec << "s\n";
    }
    std::cout << "Total search time: " << total_sec << "s\n";

    return true;
}