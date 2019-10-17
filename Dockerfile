FROM ubuntu:bionic

# USE BASH
SHELL ["/bin/bash", "-c"]

# INSTALL SYSTEM-WIDE DEPENDENCIES
RUN apt-get update && apt-get install -y --fix-missing cmake pkg-config git wget \
    build-essential libboost-all-dev zip gfortran cppad
RUN apt-get install -y --allow-unauthenticated libomp-dev libopenblas-dev liblapack-dev \
    libarpack++2-dev

# INSTALL IPOPT
COPY ./install_ipopt.sh /
RUN chmod a+rwx /install_ipopt.sh
RUN wget https://www.coin-or.org/download/source/Ipopt/Ipopt-3.12.13.zip \
    && unzip Ipopt-3.12.13.zip \
    && ./install_ipopt.sh

# INSTALL ARMADILLO
RUN wget http://sourceforge.net/projects/arma/files/armadillo-9.700.2.tar.xz \
    && tar -xf armadillo-9.700.2.tar.xz
RUN cd armadillo-9.700.2 && cmake . && make -j2 && make install

# INSTALL NLOHMANN_JSON (downloaded from github)
RUN wget https://github.com/nlohmann/json/archive/release/3.7.0.zip \
    && unzip 3.7.0.zip
RUN cd json* && mkdir build && cd build \
    && cmake .. \
    && make -j2 && make install

# REMOVE ARCHIVES
RUN rm *.tar.* && rm *.zip