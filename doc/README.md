Blackcoin More
=============

Setup
---------------------
Blackcoin More is experimental Blackcoin client and it builds the backbone of the network. However, it downloads and stores the entire history of Blackcoin transactions (which is currently several GBs); depending on the speed of your computer and network connection, the synchronization process can take anywhere from a few hours to a day or more.

To download Blackcoin More, visit [blackcoinmore.org](https://blackcoinmore.org).

Running
---------------------
The following are some helpful notes on how to run Blackcoin on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/blackmore-qt` (GUI) or
- `bin/blackmored` (headless)

### Windows

Unpack the files into a directory, and then run blackmore-qt.exe.

### OS X

Drag Blackcoin-More to your applications folder, and then run Blackcoin-More.

### Need Help?

* See the documentation at the [Bitcoin Wiki](https://en.bitcoin.it/wiki/Main_Page)
for help and more information.
* Ask for help on [#blackcoin](http://webchat.freenode.net?channels=blackcoin) on Freenode. If you don't have an IRC client use [webchat here](http://webchat.freenode.net?channels=blackcoin).
* Ask for help in [Blackcoin room](https://gitter.im/BlackCoin_Hub) on Gitter.
* Ask for help in [/r/blackcoin/](https://nm.reddit.com/r/blackcoin/) on Reddit.
* Ask for help on the [BitcoinTalk](https://bitcointalk.org/) forums, in the [Blackcoin topic](https://bitcointalk.org/index.php?topic=3017838.new#new).

Building
---------------------
The following are developer notes on how to build Blackcoin on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [OS X Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [Gitian Building Guide](gitian-building.md)

Development
---------------------
The Blackcoin repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Multiwallet Qt Development](multiwallet-qt.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- [Source Code Documentation (External Link)](https://dev.visucore.com/bitcoin/doxygen/)
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [Unit Tests](unit-tests.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)

### Resources
* Discuss on the [BitcoinTalk](https://bitcointalk.org/) forums, in the [Blackcoin topic](https://bitcointalk.org/index.php?topic=3017838.new#new).
* Discuss Blackcoin development in [Blackcoin room](https://gitter.im/BlackCoin_Hub) on Gitter.
* Discuss Blackcoin development in [Blackcoin team](https://keybase.io/team/blackcoin) on Keybase.

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)

License
---------------------
Distributed under the [MIT software license](http://www.opensource.org/licenses/mit-license.php).
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
