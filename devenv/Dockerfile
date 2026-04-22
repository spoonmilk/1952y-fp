FROM ghcr.io/browncs1952y/cs1952y-rv-gnu-tc:latest as rv-source

FROM ubuntu:jammy

# set environment variables for tzdata
ARG TZ=America/New_York
ENV TZ=${TZ}

ARG DEBIAN_FRONTEND=noninteractive

# remove unneeded .deb files
RUN rm -rf /var/lib/apt/lists/* &&\
  apt-get update && yes | unminimize && apt-get -y install sudo &&\
  useradd -m -s /bin/bash cs1952y-user && \
  echo "cs1952y-user ALL=(ALL:ALL) NOPASSWD: ALL" > /etc/sudoers.d/cs1952y-init

# configure your environment
ARG USER=cs1952y\ User
ARG EMAIL=nobody@example.com

USER cs1952y-user

# riscv is too large to build on its own in github actions so we copy it from a prebuilt container
COPY --from=rv-source /opt/riscv /opt/riscv

RUN sudo mkdir /deps && sudo chown cs1952y-user /deps && chmod u+wrx /deps &&\
  sudo apt-get update &&\
  yes | unminimize &&\
  sudo apt-get -y install\
  binutils-doc cpp-doc gcc-doc g++ gdb gdb-doc glibc-doc\
  libstdc++-10-doc make make-doc cmake gawk\
  clang lldb clang-format\
  bc curl dc git git-doc jq man emacs micro vim nano sudo wget x11-apps\
  libreadline-dev locales wamerican libssl-dev\
  build-essential m4 scons zlib1g zlib1g-dev libprotobuf-dev\
  protobuf-compiler libprotoc-dev python3-dev\
  python3-pip libboost-all-dev pkg-config \
  ninja-build\
  autoconf automake autotools-dev python3-tomli libmpc-dev libmpfr-dev libgmp-dev bison flex texinfo gperf\
  libtool patchutils libexpat-dev libglib2.0-dev libslirp-dev libncurses-dev\
  clang-tidy clang-tools clang clangd libclang-dev libclang1 libc++-dev libc++1 libc++abi-dev libc++abi1\
  liblldb-dev libllvm-ocaml-dev libomp-dev libomp5 lld llvm-dev llvm-runtime llvm python3-clang

# set up default locale
RUN sudo locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8

# install mold linker
RUN mkdir -p /deps/mold &&\
  git clone --branch stable https://github.com/rui314/mold.git /deps/mold &&\
  cd /deps/mold &&\
  sudo ./install-build-deps.sh &&\
  cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=c++ -B build &&\
  cmake --build build -j4 &&\
  sudo cmake --build build --target install

# clone and build gem5
# override GEM5_REPO at build time to use your own fork:
#   docker build --build-arg GEM5_REPO=https://github.com/yourorg/your-gem5 .
ARG GEM5_REPO=https://github.com/browncs1952y/gem5-assignments-stencil
RUN git clone $GEM5_REPO /home/cs1952y-user/gem5 && cd /home/cs1952y-user/gem5 &&\
  sudo mkdir /gem5_build /gem5_cache && sudo chown cs1952y-user /gem5_build /gem5_cache && chmod u+wrx /gem5_build /gem5_cache &&\
  scons defconfig /gem5_build build_opts/RISCV --ignore-style &&\
  scons setconfig /gem5_build USE_KVM=y --ignore-style &&\
  scons setconfig /gem5_build M5_BUILD_CACHE=/gem5_cache --ignore-style &&\
  scons /gem5_build/gem5.debug -j4 --linker=mold --limit-ld-memory-usage --ignore-style &&\
  echo "alias build-gem5=\"scons /gem5_build/gem5.debug -j8 --linker=mold --limit-ld-memory-usage --ignore-style\"" >> /home/cs1952y-user/.bashrc

RUN echo "export PATH=\"$PATH:/opt/riscv/bin\"" >> /home/cs1952y-user/.bashrc

RUN rm -f ~/.bash_logout

WORKDIR /home/cs1952y-user
CMD ["/bin/bash", "-l"]
