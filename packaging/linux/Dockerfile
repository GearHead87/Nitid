# Local release build environment: the same Ubuntu 24.04 + packages as the GitHub Actions runner.
# Built and used by packaging/linux/docker-build.sh; the image (nitid-build:ubuntu-24.04) is kept between
# releases. Sources are bind-mounted, build output lives in a Docker volume, so the image stays small.
FROM ubuntu:24.04
COPY install-build-deps.sh /tmp/install-build-deps.sh
RUN bash /tmp/install-build-deps.sh && rm /tmp/install-build-deps.sh
ENV PATH=/usr/local/cuda/bin:$PATH
WORKDIR /src
