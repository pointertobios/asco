FROM archlinux:latest

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

LABEL org.opencontainers.image.title="asco-build (Archlinux + LLVM)"
LABEL org.opencontainers.image.description="ASCO build environment - Arch Linux build image with clang/llvm, cmake, ninja, ccache"
LABEL org.opencontainers.image.source="https://github.com/pointertobios/asco"
LABEL org.opencontainers.image.licenses="MIT"

ENV PATH="/usr/lib/ccache/bin:${PATH}"

RUN set -eux;                                   \
    pacman -Sy --noconfirm archlinux-keyring && \
    pacman -Syu --noconfirm &&                  \
    pacman-key --init &&                        \
    pacman-key --populate archlinux

RUN set -eux;                                   \
    pacman -S --noconfirm --needed              \
    base-devel                                  \
    cmake                                       \
    ninja                                       \
    clang                                       \
    llvm                                        \
    lld                                         \
    lldb                                        \
    pkgconf                                     \
    ccache                                      \
    git                                         \
    liburing                                    \
    ca-certificates;                            \
    update-ca-trust

ARG USERNAME=builder
ARG USER_UID=1000
ARG USER_GID=1000
RUN set -eux;                                   \
    groupadd -g "${USER_GID}" "${USERNAME}" &&  \
    useradd -m -u "${USER_UID}" -g "${USER_GID}" -s /bin/bash "${USERNAME}"

ENV CC=clang                                    \
    CXX=clang++                                 \
    CMAKE_C_COMPILER=clang                      \
    CMAKE_CXX_COMPILER=clang++                  \
    CMAKE_C_COMPILER_LAUNCHER=ccache            \
    CMAKE_CXX_COMPILER_LAUNCHER=ccache          \
    CCACHE_DIR=/home/${USERNAME}/.cache/ccache  \
    CCACHE_MAXSIZE=2G                           \
    CCACHE_COMPRESS=1

RUN set -eux;                                   \
    install -d -m 0755 -o "${USER_UID}" -g "${USER_GID}" "${CCACHE_DIR}"

RUN set -eux;                                   \
    pacman -Scc --noconfirm

RUN mkdir -p /work
WORKDIR /work
USER ${USERNAME}
