#!/bin/bash

if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root" 1>&2
    exit 1
fi

# Requires debian
dpkg-query -l cmake libpcap-dev libxerces-c2-dev libpcre3-dev flex bison 2>&1 > /dev/null
if [[ $? -ne 0 ]]; then
   echo "Need to install dependencies" 1>&2
   apt-get install cmake libpcap-dev libxerces-c2-dev libpcre3-dev flex bison
fi

cd src &&
cmake . &&
make &&

cd .. &&
cp bin/libn*.so /usr/local/lib &&
ldconfig &&
cp -R include/* /usr/local/include &&

# Note: We aren't compiling installing the tools that come with NetBee.

echo "NetBee installed to /usr/local."
