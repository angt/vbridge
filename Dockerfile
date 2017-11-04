FROM ubuntu:16.04

RUN apt update
RUN apt install -y --no-install-recommends make gcc \
	libx11-dev libxfixes-dev libxext-dev libxi-dev libxtst-dev libxrandr-dev \
	libkrb5-dev libpam0g-dev \
	libssl-dev libcap-dev
RUN mkdir /home/vbridge

COPY . /home/vbridge

RUN useradd vbridge && chown -R vbridge /home/vbridge
USER vbridge

WORKDIR /home/vbridge
RUN make
