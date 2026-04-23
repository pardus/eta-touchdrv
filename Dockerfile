FROM pardus/yirmibes:latest

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    debhelper \
    dh-sequence-dkms \
    dkms \
    devscripts \
    equivs

RUN apt-get install -y --no-install-recommends libx11-6 # required by calibration

# RUN rm -rf /var/lib/apt/lists/*

WORKDIR /work