#!/bin/sh
# Fetch and build the pinned libiio v1 into deps/install (never touches the system).
set -eu
COMMIT=cee4310e47198ba2d20e55b482061e3ab30d35d2
# Bump when the configure options below change, to force a rebuild
STAMP="$COMMIT cfg2"
URL=https://github.com/analogdevicesinc/libiio.git
here=$(cd "$(dirname "$0")" && pwd)
src="$here/src/libiio"
prefix="$here/install"

if [ -f "$prefix/include/iio/iio.h" ] && [ -f "$prefix/.commit" ] &&
   [ "$(cat "$prefix/.commit")" = "$STAMP" ]; then
	echo "libiio $COMMIT already installed in $prefix"
	exit 0
fi

if [ ! -d "$src/.git" ]; then
	mkdir -p "$src"
	git -C "$src" init -q
	git -C "$src" remote add origin "$URL"
fi
git -C "$src" fetch -q --depth 1 origin "$COMMIT"
git -C "$src" checkout -q FETCH_HEAD

cmake -S "$src" -B "$src/build" -G Ninja \
	-DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DWITH_EMU_BACKEND=ON -DWITH_EMU_BACKEND_DYNAMIC=OFF -DWITH_XML_BACKEND=ON \
	-DWITH_USB_BACKEND=OFF -DWITH_SERIAL_BACKEND=OFF -DWITH_IIOD=OFF \
	-DHAVE_DNS_SD=OFF \
	-DWITH_UTILS=OFF -DWITH_EXAMPLES=OFF -DWITH_DOC=OFF
cmake --build "$src/build"
cmake --install "$src/build"
echo "$STAMP" > "$prefix/.commit"
