# Linux-only build/test and runtime images. No registry publication in this file.
FROM ubuntu:26.04 AS build-env
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential cmake ninja-build git ca-certificates libcurl4-openssl-dev libssl-dev python3 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
CMD ["bash"]

FROM build-env AS build
COPY . .
RUN bash scripts/check-release.sh \
    && bash scripts/package.sh -G Ninja -DAXON_WARNINGS_AS_ERRORS=ON \
    && cmake --install build-alpha --prefix /opt/axon

# Export the native Linux validation archive with BuildKit --output.
FROM scratch AS artifacts
COPY --from=build /src/dist/ /

FROM ubuntu:26.04 AS runtime
RUN apt-get update \
    && apt-get install -y --no-install-recommends libstdc++6 ca-certificates libcurl4t64 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 axon \
    && useradd --uid 10001 --gid 10001 --create-home --shell /usr/sbin/nologin axon \
    && mkdir /work \
    && chown 10001:10001 /work
COPY --from=build /opt/axon/bin/ /opt/axon/bin/
COPY --from=build /opt/axon/share/axon/ /opt/axon/share/axon/
ENV HOME=/home/axon
WORKDIR /work
USER 10001:10001
ENTRYPOINT ["/opt/axon/bin/axon"]
CMD ["version"]
