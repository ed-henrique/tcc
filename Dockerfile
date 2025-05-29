FROM ubuntu:20.04

ARG DEBIAN_FRONTEND=noninteractive
ARG BUILD_DIR="/tcc/ns-allinone-3.32/"
ARG ROOT_DIR="${BUILD_DIR}/ns-3.32"
ARG SRC="${ROOT_DIR}/src"
ARG SCRATCH="${ROOT_DIR}/scratch"

ENV TCC_BUILD_MODE="debug"

RUN sed -i 's|http://archive.ubuntu.com|http://mirrors.163.com|g' /etc/apt/sources.list \
    && sed -i 's|http://security.ubuntu.com|http://mirrors.163.com|g' /etc/apt/sources.list 

RUN apt-get update && apt-get install -y \
  build-essential \
  autoconf \
  automake \
  libxmu-dev \
  python3-pygraphviz \
  cvs \
  mercurial \
  bzr \
  git \
  cmake \
  p7zip-full \
  python3-matplotlib \
  python-tk \
  python3-dev \
  qt5-qmake \
  qt5-default \
  gnuplot-x11 \
  wireshark

WORKDIR /tcc

RUN curl -o ns-allinone-3.32.tar.bz2 https://www.nsnam.org/releases/ns-allinone-3.32.tar.bz2 && \
  tar -xvjf ns-allinone-3.32.tar.bz2 && \
  rm ns-allinone-3.32.tar.bz2

WORKDIR $BUILD_DIR

RUN ./build.py --enable-examples --enable-tests

WORKDIR $ROOT_DIR

RUN rm -rf "${SRC}/lte" "${SRC}/propagation" && \
  git clone https://github.com/tudo-cni/ns3-lena-nb.git "${SRC}/lte" && \
  git clone https://github.com/tudo-cni/ns3-propagation-winner-plus.git "${SRC}/propagation"

RUN cp "${SRC}/lte/examples/lena-nb-5G-scenario.cc" "${SCRATCH}/sim.cc"
RUN ./waf clean && \
  CXXFLAGS='-O3 -w' ./waf -d "${TCC_BUILD_MODE}" configure \
    --enable-examples \
    --enable-modules=lte \
    --disable-python && \
  ./waf build

ENTRYPOINT ["./waf", "--run"]
CMD ["scratch/sim"]
