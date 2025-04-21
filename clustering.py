import os
import pandas as pd
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
    return loglines

cleaned_logs = preprocess_logs(loglines)

# Step 3: TF-IDF Vectorization
vectorizer = TfidfVectorizer(max_features=1000)
log_vectors = vectorizer.fit_transform(cleaned_logs)

# Step 5: DBSCAN clustering
n_clusters = 7
kmeans = KMeans(n_clusters=n_clusters, random_state=42)

# Get the cluster labels for each log line
cluster_labels = kmeans.fit_predict(log_vectors)

# # Step 5: Combine the original loglines with their respective cluster IDs
log_with_clusters = pd.DataFrame({
    "logline": cleaned_logs,
    "cluster_id": cluster_labels
})

# Step 6: Display the clustering results (first 5 entries)
print(log_with_clusters.head(5))

output_file = "clustered_logs.txt"  # You can change this to any other file format

# Combine the loglines with their cluster ids and save to a file
log_with_clusters['logline_with_cluster'] = log_with_clusters['logline'].str.strip() + "|ClusterID: " + log_with_clusters['cluster_id'].astype(str)

# Save to a text file (one logline per line, no extra spaces or newlines)
with open(output_file, 'w') as f:
    for log in log_with_clusters['logline_with_cluster']:
        f.write(log + '\n')  # Write each log with the cluster ID on one line

print(f"Clustering results saved to {output_file}")

