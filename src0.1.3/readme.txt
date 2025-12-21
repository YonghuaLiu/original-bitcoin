BitCoin v0.1.3 ALPHA

Copyright (c) 2009 Satoshi Nakamoto
Copyright (c) 2025 Digital People Tribe
Distributed under the MIT/X11 software license, see the accompanying
file license.txt or http://www.opensource.org/licenses/mit-license.php.
This product includes software developed by the OpenSSL Project for use in
the OpenSSL Toolkit (http://www.openssl.org/).  This product includes
cryptographic software written by Eric Young (eay@cryptsoft.com).


Compilers Supported
-------------------
MinGW64 GCC (15.2.0)


Dependencies
------------
Libraries you need to obtain separately to build:

              default path   download
wxWidgets      \wxWidgets     http://www.wxwidgets.org/downloads/
OpenSSL        \OpenSSL       http://www.openssl.org/source/
Berkeley DB    \DB            http://www.oracle.com/technology/software/products/berkeley-db/index.html
Boost          \Boost         http://www.boost.org/users/download/

Their licenses:
wxWidgets      LGPL 2.1 with very liberal exceptions
OpenSSL        Old BSD license with the problematic advertising requirement
Berkeley DB    New BSD license with additional requirement that linked software must be free open source
Boost          MIT-like license


wxWidgets
----------
(3.2.8.1)

OpenSSL
-------
(OpenSSL 3.6.0)


Berkeley DB
-----------
(Berkeley DB 4.7.25)
MinGW64 with MSYS:
cd /DB/build_unix
../dist/configure --prefix=/usr/local/bdb-4.7.25 \
  --enable-cxx \
  CFLAGS="-std=gnu11" \
  CXXFLAGS="-std=gnu++11"
make


Boost
-----
(1.89.0)
