#!/bin/bash

if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root" 1>&2
    exit 1
fi

rm -r /usr/local/lib/libnb*.so &&
rm -r /usr/local/include/nb*.h &&
rm -rf /usr/local/include/old_headers &&
ldconfig

echo "NetBee removed from /usr/local."
