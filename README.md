# LogPress: Optimized Compression and Retrieval of Unstructured Logs.

## Team 6 (during presentation)
The collaborators for this project and the work performed by them,
- ndudhel [Neel Dudheliya](https://github.com/Neel317) worked on chunk-based approach
- tgandhi [Tanay Gandhi](https://github.com/tanay306) worked on clustering and naive approach
- pjibhak [Pranav Jibhakate](https://github.com/pranavJibhakate) worked on distributed approach.

## Which branches have our latest code:
Each branch has different approaches in them and hence we were not able to merge it into one. The code in these branches are used during our Demo and test results. Every branch has its own owner(collaborator), setup guide and details in the individual Readme.md
- Naive approach - [dev](https://github.com/tanay306/LogPress/tree/dev)
- Chunk based approach - [parallel-search](https://github.com/tanay306/LogPress/tree/parallel-search)
- Clustering approach (K-Means) - [clustering](https://github.com/tanay306/LogPress/tree/clustering)
- Distributed approach - [distributed-with-docker](https://github.com/tanay306/LogPress/tree/distributed-with-docker)

## Dataset
For all testing all the approaches you will need datasets. We have used datasets from [Loghub](https://github.com/logpai/loghub) and [Zenodo](https://zenodo.org/). The datasets we have used in Results/Evaluation are:
| Dataset | Compressed/Download Size | Uncompressed/Original Size |
|---------|--------------------------|-------------------|
| [Mac](https://zenodo.org/records/8196385/files/Mac.tar.gz?download=1) | 1.5 MB | 16.9 MB |
| [OpenSSH](https://zenodo.org/records/8196385/files/SSH.tar.gz?download=1) | 4.6 MB | 70.02 MB |
| [Android_v1](https://zenodo.org/records/8196385/files/Android_v1.zip?download=1) | 24.9 MB | 183.37 MB |
| [HDFS_v1](https://zenodo.org/records/8196385/files/HDFS_v1.zip?download=1) | 186.6 MB | 1.47 GB |
| [Hive](https://zenodo.org/records/7094921) | 128.5 MB | 2.13 GB |
| [Spark](https://zenodo.org/records/8196385/files/Spark.tar.gz?download=1) | 183.5 MB | 2.75 GB |