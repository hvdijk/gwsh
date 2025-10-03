FROM debian:bookworm
RUN dpkg --add-architecture arm64
RUN apt-get -y update
RUN apt-get -y install devscripts gcc-aarch64-linux-gnu git groff libedit-dev:amd64 libedit-dev:arm64 mandoc reprepro
