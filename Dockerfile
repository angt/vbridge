FROM ubuntu:17.10
ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update \
 && apt-get install -y \
    make gcc \
    libssl-dev \
    libpam0g-dev \
    libcap-dev \
    libkrb5-dev \
    libx11-dev \
    libxext-dev \
    libxfixes-dev \
    libxi-dev \
    libxrandr-dev \
    libxrender-dev \
    libxdamage-dev \
    libxtst-dev

RUN mkdir -p /tmp/build
COPY . /tmp/build

WORKDIR /tmp/build
RUN make
