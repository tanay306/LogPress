import os
import pandas as pd
import re
from sklearn.cluster import KMeans
from sklearn.feature_extraction.text import TfidfVectorizer
from sklearn.cluster import DBSCAN
from sklearn.decomposition import TruncatedSVD
from collections import Counter

logfile_path = "Mac.log"  # Update this with your log file path

# Read the log file into a DataFrame or a list of log lines
with open(logfile_path, "r") as file:
    loglines = file.readlines()

# If your logs are structured in a table format, you could load them into a DataFrame
# Example: log_df = pd.read_csv(logfile_path, delimiter="\t")  # adjust accordingly
print(f"Total loglines: {len(loglines)}")

threshold = len(loglines) / 100

def preprocess_logs(loglines):
    return loglines

def extract_templates_and_frequencies(loglines):
    num_regex = re.compile(r"[+-]?\d+(?:[._:\-]?\d+)*")
    template_freq = Counter()  # To count occurrences of each template

    for line in loglines:
        start_pos = 0
        template = ""
        
        # Search for numeric values in the line
        for match in num_regex.finditer(line):
            match_start = match.start()
            match_end = match.end()
            
            # Add the part of the line before the numeric token to the template
            template += line[start_pos:match_start]
            template += "<VAR>"  # Placeholder for the number
            
            # Update start position for the next search
            start_pos = match_end
        
        # Add the remaining part of the line after the last numeric token
        template += line[start_pos:]
        
        # Increment frequency of this template
        template_freq[template] += 1

    return template_freq

# Function to get the most frequent template
def most_frequent_template(template_freq, threshold=500):
    # Get the template with the highest frequency
    high_freq_templates = [tpl for tpl, freq in template_freq.items() if freq > threshold]
    
    # Return the number of such templates
    return len(high_freq_templates)

template_freq = extract_templates_and_frequencies(loglines)

cleaned_logs = preprocess_logs(loglines)

# Step 3: TF-IDF Vectorization
vectorizer = TfidfVectorizer(max_features=1000)
log_vectors = vectorizer.fit_transform(cleaned_logs)

# Step 5: KMeans clustering
n_clusters = most_frequent_template(template_freq, threshold)

print("Dividing loglines in : ", n_clusters, " clustrers")
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

