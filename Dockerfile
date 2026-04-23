FROM pardus/yirmibes:latest

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    debhelper \
    dh-sequence-dkms \
    dkms \
    devscripts \
    equivs

# required by calibration
RUN apt-get install -y --no-install-recommends libx11-6 

# clang build
RUN apt-get update && apt-get install -y \
    clang \
    llvm \
    lld

# RUN rm -rf /var/lib/apt/lists/*

WORKDIR /work