# build stage
FROM golang:1.22-alpine AS builder
RUN apk add --no-cache make \
    gcc g++ musl-dev pkgconfig sqlite-dev zlib-dev

WORKDIR /app
COPY . .
RUN go build dbserver.go
CMD ./dbserver

