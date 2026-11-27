# Image du site web AS Volley, pour Railway (ou tout hebergeur qui accepte Docker).
# Railway detecte ce fichier tout seul et fournit la variable PORT.
# A definir dans Railway : DATABASE_URL (adresse Neon).

# ---------------------------------------------------------------- compilation
FROM ubuntu:24.04 AS compilation
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ cmake make pkg-config \
        libpqxx-dev libcpp-httplib-dev nlohmann-json3-dev libqrcodegencpp-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target site -j"$(nproc)"

# ---------------------------------------------------------------- image finale (legere)
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        libpqxx-7.8t64 libcpp-httplib0.14t64 libqrcodegencpp1 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home asvolley
WORKDIR /app
COPY --from=compilation /src/build/site ./site
COPY web ./web
USER asvolley
ENV WEB_DIR=/app/web PORT=8080
EXPOSE 8080
CMD ["./site"]
