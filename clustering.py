import os
import re
import pandas as pd
import sqlite3
from sklearn.feature_extraction.text import TfidfVectorizer
from sklearn.cluster import KMeans

logfile_path = "HDFS.log"
sqlite_db = "template.db"
output_txt = "clustered_log.txt"

# Step 1: Read logs
with open(logfile_path, "r") as file:
    loglines = [line.strip() for line in file.readlines()]

print(f"Total loglines: {len(loglines)}")

# Maps and counters
ip_map, block_map, path_map, ts_map = {}, {}, {}, {}
ip_count = block_count = path_count = ts_count = 1

compressed_logs = []

# SQLite setup
conn = sqlite3.connect(sqlite_db)
cur = conn.cursor()

cur.execute("CREATE TABLE IF NOT EXISTS ips (id INTEGER PRIMARY KEY, value TEXT UNIQUE)")
cur.execute("CREATE TABLE IF NOT EXISTS blocks (id INTEGER PRIMARY KEY, value TEXT UNIQUE)")
cur.execute("CREATE TABLE IF NOT EXISTS paths (id INTEGER PRIMARY KEY, value TEXT UNIQUE)")
cur.execute("CREATE TABLE IF NOT EXISTS timestamps (id INTEGER PRIMARY KEY, value TEXT UNIQUE)")
cur.execute("CREATE TABLE IF NOT EXISTS clustered_logs (id INTEGER PRIMARY KEY AUTOINCREMENT, compressed_log TEXT, cluster_id INTEGER)")

# Step 2: Replace & collect
for line in loglines:
    original = line

    # Replace timestamp
    timestamps = re.findall(r"\b\d{6} \d{6}\b", line)
    for ts in timestamps:
        if ts not in ts_map:
            ts_map[ts] = ts_count
            cur.execute("INSERT OR IGNORE INTO timestamps (id, value) VALUES (?, ?)", (ts_count, ts))
            ts_count += 1
        line = line.replace(ts, f"^{ts_map[ts]}")

    # Replace block IDs
    blocks = re.findall(r"blk_[-\d]+", line)
    for blk in blocks:
        if blk not in block_map:
            block_map[blk] = block_count
            cur.execute("INSERT OR IGNORE INTO blocks (id, value) VALUES (?, ?)", (block_count, blk))
            block_count += 1
        line = line.replace(blk, f"#{block_map[blk]}")

    # Replace IPs
    ips = re.findall(r"\d+\.\d+\.\d+\.\d+", line)
    for ip in ips:
        if ip not in ip_map:
            ip_map[ip] = ip_count
            cur.execute("INSERT OR IGNORE INTO ips (id, value) VALUES (?, ?)", (ip_count, ip))
            ip_count += 1
        line = line.replace(ip, f"@{ip_map[ip]}")

    # Replace file paths
    paths = re.findall(r"/[\w/\.\-]+", line)
    for path in paths:
        if path not in path_map:
            path_map[path] = path_count
            cur.execute("INSERT OR IGNORE INTO paths (id, value) VALUES (?, ?)", (path_count, path))
            path_count += 1
        line = line.replace(path, f"/{path_map[path]}")

    compressed_logs.append(line)

conn.commit()

# Step 3: Clustering
vectorizer = TfidfVectorizer(max_features=1000)
X = vectorizer.fit_transform(compressed_logs)
kmeans = KMeans(n_clusters=7, random_state=42)
cluster_labels = kmeans.fit_predict(X)

# Step 4: Store clustered log output
for cline, cid in zip(compressed_logs, cluster_labels):
    cur.execute("INSERT INTO clustered_logs (compressed_log, cluster_id) VALUES (?, ?)", (cline, cid))

with open(output_txt, "w") as f:
    for line, cid in zip(compressed_logs, cluster_labels):
        f.write(f"{line} | Cluster ID: {cid}\n")

conn.commit()
conn.close()

print(f"\n✅ Compressed and clustered logs written to {output_txt}")
print(f"✅ Replacement values stored in {sqlite_db}")
