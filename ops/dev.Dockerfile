# Build and benchmark environment. The benchmark numbers in the README were
# measured inside this image.
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential cmake python3 util-linux linux-tools-generic gzip ca-certificates \
    && rm -rf /var/lib/apt/lists/*
# linux-tools-generic ships perf for Ubuntu's generic kernel; link it so it runs
# on whatever kernel the container is on.
RUN ln -sf "$(ls -d /usr/lib/linux-tools/*/ | head -1)perf" /usr/local/bin/perf
WORKDIR /src
