FROM ubuntu:22.04

RUN apt-get update && apt-get install -y cmake g++ make unzip && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j4

EXPOSE 6379

CMD ["./build/vaultdb"]
