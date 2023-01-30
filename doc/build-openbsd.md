OpenBSD build guide
======================
(updated for OpenBSD 6.9)

This guide describes how to build blackmored, blackmore-qt, and command-line utilities on OpenBSD.

Preparation
-------------

Run the following as root to install the base dependencies for building:

```bash
pkg_add git gmake libevent libtool boost
pkg_add qt5 # (optional for enabling the GUI)
pkg_add autoconf # (select highest version, e.g. 2.69)
pkg_add automake # (select highest version, e.g. 1.16)
pkg_add python # (select highest version, e.g. 3.8)
pkg_add bash

git clone https://github.com/CoinBlack/blackcoin-more.git
```

See [dependencies.md](dependencies.md) for a complete overview.

**Important**: From OpenBSD 6.2 onwards a C++11-supporting clang compiler is
part of the base image, and while building it is necessary to make sure that
this compiler is used and not ancient g++ 4.2.1. This is done by appending
`CC=cc CXX=c++` to configuration commands. Mixing different compilers within
the same executable will result in errors.

### Building BerkeleyDB

BerkeleyDB is only necessary for the wallet functionality. To skip this, pass
`--disable-wallet` to `./configure` and skip to the next section.

It is recommended to use Berkeley DB 6.2. You cannot use the BerkeleyDB library
from ports, for the same reason as boost above (g++/libstd++ incompatibility).

```bash
# Pick some path to install BDB to, here we create a directory within the blackcoin directory
BITCOIN_ROOT=$(pwd)
BDB_PREFIX="${BITCOIN_ROOT}/build"
mkdir -p $BDB_PREFIX

# Fetch the source and verify that it is not tampered with
curl -o db-6.2.38.tar.gz 'http://download.oracle.com/berkeley-db/db-6.2.38.tar.gz'
echo 'a4c88b51523684ed0dc8abeacf1f0aa53249c8a057e3cd581dca0159a03cb1c3  db-6.2.38.tar.gz' | sha256 -c
# MUST output: (SHA256) db-6.2.38.tar.gz: OK
tar -xzf db-6.2.38.tar.gz

# Build the library and install to specified prefix
cd db-6.2.38/build_unix/
#  Note: Do a static build so that it can be embedded into the executable, instead of having to find a .so at runtime
../dist/configure --enable-cxx --disable-shared --with-pic --prefix=$BDB_PREFIX CC=egcc CXX=eg++ CPP=ecpp
make install # do NOT use -jX, this is broken
```

### Building Blackcoin More

**Important**: Use `gmake` (the non-GNU `make` will exit with an error).

Preparation:
```bash

# Replace this with the autoconf version that you installed. Include only
# the major and minor parts of the version: use "2.69" for "autoconf-2.69p2".
export AUTOCONF_VERSION=2.69

# Replace this with the automake version that you installed. Include only
# the major and minor parts of the version: use "1.16" for "automake-1.16.1".
export AUTOMAKE_VERSION=1.16

./autogen.sh
```
Make sure `BDB_PREFIX` is set to the appropriate path from the above steps.

Note that building with external signer support currently fails on OpenBSD,
hence you have to explicitly disable it by passing the parameter
`--disable-external-signer` to the configure script.
(Background: the feature requires the header-only library boost::process, which
is available on OpenBSD 6.9 via Boost 1.72.0, but contains certain system calls
and preprocessor defines like `waitid()` and `WEXITED` that are not available.)

To configure with wallet:
```bash
./configure --with-gui=no --disable-external-signer CC=cc CXX=c++ \
    BDB_LIBS="-L${BDB_PREFIX}/lib -ldb_cxx-6.2" \
    BDB_CFLAGS="-I${BDB_PREFIX}/include" \
    MAKE=gmake
```

To configure without wallet:
```bash
./configure --disable-wallet --with-gui=no --disable-external-signer CC=cc CXX=c++ MAKE=gmake
```

To configure with GUI:
```bash
./configure --with-gui=yes --disable-external-signer CC=cc CXX=c++ \
    BDB_LIBS="-L${BDB_PREFIX}/lib -ldb_cxx-4.8" \
    BDB_CFLAGS="-I${BDB_PREFIX}/include" \
    MAKE=gmake
```

Build and run the tests:
```bash
gmake # use "-j N" here for N parallel jobs
gmake check
```

Resource limits
-------------------

If the build runs into out-of-memory errors, the instructions in this section
might help.

The standard ulimit restrictions in OpenBSD are very strict:

    data(kbytes)         1572864

This is, unfortunately, in some cases not enough to compile some `.cpp` files in the project,
(see issue [#6658](https://github.com/bitcoin/bitcoin/issues/6658)).
If your user is in the `staff` group the limit can be raised with:

    ulimit -d 3000000

The change will only affect the current shell and processes spawned by it. To
make the change system-wide, change `datasize-cur` and `datasize-max` in
`/etc/login.conf`, and reboot.

