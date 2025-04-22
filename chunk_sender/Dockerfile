# build stage
FROM golang:1.22-alpine AS builder
RUN apk add --no-cache make \
    gcc g++ musl-dev pkgconfig sqlite-dev zlib-dev

WORKDIR /app
COPY . .
RUN go build chunk_sender.go
RUN dos2unix ./HDFS2.log 
CMD echo "Run iteractive"
