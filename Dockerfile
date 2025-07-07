FROM debian:bookworm

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

RUN apt-get update && \
    apt-get install -y \
        git \
        g++ \
        python3 \
        python3-dev \
        python3-setuptools \
        qtbase5-dev \
        qtchooser \
        qt5-qmake \
        qtbase5-dev-tools \
        gir1.2-goocanvas-2.0 \
        python3-gi \
        python3-gi-cairo \
        python3-pygraphviz \
        gir1.2-gtk-3.0 \
        ipython3 \
        openmpi-bin \
        openmpi-common \
        openmpi-doc \
        libopenmpi-dev \
        mercurial \
        unzip \
        gdb \
        valgrind \
        pkg-config \
        doxygen \
        graphviz \
        imagemagick \
        texlive \
        texlive-extra-utils \
        texlive-latex-extra \
        texlive-font-utils \
        dvipng \
        latexmk \
        python3-sphinx \
        dia \
        gsl-bin \
        libgsl-dev \
        libgslcblas0 \
        tcpdump \
        sqlite3 \
        libsqlite3-dev \
        libxml2 \
        libxml2-dev \
        libc6-dev \
        libc6-dev-i386 \
        libclang-dev \
        llvm-dev \
        automake \
        libgtk-3-dev \
        vtun \
        lxc \
        uml-utilities \
        libboost-all-dev \
        castxml \
        cmake \
        wget \
        vim \
        python3-pip

RUN python3 -m pip install --user --break-system-packages cxxfilt pygccxml

WORKDIR /usr/ns3

RUN wget http://www.nsnam.org/release/ns-allinone-3.32.tar.bz2 && \
    tar xjf ns-allinone-3.32.tar.bz2

WORKDIR /usr/ns3/ns-allinone-3.32/ns-3.32

RUN mkdir /logs && \
    rm -rf src/lte src/propagation && \
    git clone https://github.com/tudo-cni/ns3-lena-nb.git src/lte && \
    git clone https://github.com/tudo-cni/ns3-propagation-winner-plus.git src/propagation

RUN CXXFLAGS="-O3 -w" ./waf configure --build-profile=debug --enable-examples --disable-python
RUN ./waf -v

ENTRYPOINT ["./waf"]
CMD ["--help"]
