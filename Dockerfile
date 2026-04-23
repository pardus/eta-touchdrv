FROM pardus/yirmibes:latest

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       debhelper \
       dh-sequence-dkms \
       dkms \
       devscripts \
       equivs

WORKDIR /work/eta-touchdrv

CMD ["/bin/sh", "-lc", "test -f debian/control || { echo 'Mount the repo at /work/eta-touchdrv'; exit 1; }; mkdir -p /out && mk-build-deps -i -r -t 'apt-get -y --no-install-recommends' debian/control && dpkg-buildpackage -b -uc -us && cp -v /work/*.udeb /work/*.deb /work/*.changes /work/*.buildinfo /out/ 2>/dev/null || true"]
