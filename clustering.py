import os
import pandas as pd
import matplotlib.pyplot as plt
import re
from sklearn.cluster import KMeans
from sklearn.feature_extraction.text import TfidfVectorizer
from sklearn.cluster import DBSCAN
from sklearn.decomposition import TruncatedSVD

logfile_path = "HDFS.log"  # Update this with your log file path

# Read the log file into a DataFrame or a list of log lines
with open(logfile_path, "r") as file:
    loglines = file.readlines()

# If your logs are structured in a table format, you could load them into a DataFrame
# Example: log_df = pd.read_csv(logfile_path, delimiter="\t")  # adjust accordingly
print(f"Total loglines: {len(loglines)}")

def preprocess_logs(loglines):
    patterns = [
        (r"\bblk_[\-\d]+", "<BLOCK>"),
        (r"\d+\.\d+\.\d+\.\d+(:\d+)?", "<IP_PORT>"),  # IP with optional port
        (r"/[\w/\.\-]+", "<PATH>"),
        (r"\b\d{6} \d{6}", "<TIMESTAMP>"),  # e.g. 081111 032233
        (r"\b\d+\b", "<NUM>"),  # Replace standalone numbers
    ]

    cleaned_logs = []
    for line in loglines:
        line = line.lower()
        for pattern, repl in patterns:
            line = re.sub(pattern, repl, line)
        cleaned_logs.append(line.strip())
    return cleaned_logs

cleaned_logs = preprocess_logs(loglines)

# Step 3: TF-IDF Vectorization
vectorizer = TfidfVectorizer(max_features=1000)
log_vectors = vectorizer.fit_transform(cleaned_logs)

# Step 4: Dimensionality reduction (compression)
svd = TruncatedSVD(n_components=100, random_state=42)
log_vectors_reduced = svd.fit_transform(log_vectors)

# Step 5: DBSCAN clustering
dbscan = DBSCAN(eps=0.5, min_samples=3, metric='euclidean')
dbscan.fit(log_vectors_reduced)

# Get the cluster labels for each log line
# cluster_labels = kmeans.predict(log_vectors)

# # Step 5: Combine the original loglines with their respective cluster IDs
# log_with_clusters = pd.DataFrame({
#     "logline": cleaned_logs,
#     "cluster_id": cluster_labels
# })

cluster_labels = dbscan.labels_

# Combine logs with cluster labels
log_with_clusters = pd.DataFrame({
    "logline": cleaned_logs,
    "cluster_id": cluster_labels
})

# Step 6: Display the clustering results (first 5 entries)
print(log_with_clusters.head(5))

output_file = "clustered_logs.txt"  # You can change this to any other file format

# Combine the loglines with their cluster ids and save to a file
log_with_clusters['logline_with_cluster'] = log_with_clusters['logline'] + " | Cluster ID: " + log_with_clusters['cluster_id'].astype(str)

# Save to a text file (one logline per line)
log_with_clusters['logline_with_cluster'].to_csv(output_file, index=False, header=False)

print(f"Clustering results saved to {output_file}")
