# build stage
FROM golang:1.22-alpine AS builder
RUN apk add --no-cache make \
    gcc g++ musl-dev pkgconfig sqlite-dev zlib-dev curl-dev

WORKDIR /app
COPY . .
RUN make

# ensure the binary is executable
RUN chmod +x ./chunk_reciever/chunk_reciever

# switch into the folder & run the binary
WORKDIR /app/chunk_reciever
CMD ["./chunk_reciever", "8081"]
