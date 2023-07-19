Blackcoin More
=====================================
[![build](https://github.com/CoinBlack/blackcoin-more/actions/workflows/build.yml/badge.svg?branch=23.x)](https://github.com/CoinBlack/blackcoin-more/actions/workflows/build.yml)

https://blackcoinmore.org

What is Blackcoin?
----------------

Blackcoin is a decentralised digital currency with near-instant transaction speeds and negligible transaction fees built upon Proof of Stake 3.0 (PoSV3, BPoS) as introduced by the Blackcoin development team.

What is Blackcoin More?
----------------

Blackcoin More is the name of open source software which enables use of the Blackcoin protocol. 

For more information, as well as an immediately useable, binary version of
the Blackcoin More software, see https://blackcoinmore.org.

License
-------

Blackcoin More is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

Development Process
-------------------

The `master` branch is regularly built (see `doc/build-*.md` for instructions) and tested, but it is not guaranteed to be
completely stable. [Tags](https://github.com/bitcoin/bitcoin/tags) are created
regularly from release branches to indicate new official, stable release versions of Bitcoin Core.

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md)
and useful hints for developers can be found in [doc/developer-notes.md](doc/developer-notes.md).

The best place to get started is to join Blackcoin Discord: https://discord.blackcoin.nl

Testing
-------

Testing and code review might be the bottleneck for development. Please help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write [unit tests](src/test/README.md) for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run
(assuming they weren't disabled in configure) with: `make check`. Further details on running
and extending unit tests can be found in [/src/test/README.md](/src/test/README.md).

There are also [regression and integration tests](/test), written
in Python.
These tests can be run (if the [test dependencies](/test) are installed) with: `test/functional/test_runner.py`

The CI (Continuous Integration) systems make sure that every pull request is built for Windows, Linux, and macOS,
and that unit/sanity tests are run automatically.

### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than the developer who wrote the
code. This is especially important for large or high-risk changes. It is useful
to add a test plan to the pull request description if testing the changes is
not straightforward.

Translations
------------

Changes to translations as well as new translations can be submitted to
[Bitcoin Core's Transifex page](https://www.transifex.com/bitcoin/bitcoin/).

Translations are periodically pulled from Transifex and merged into the git repository. See the
[translation process](doc/translation_process.md) for details on how this works.

**Important**: We do not accept translation changes as GitHub pull requests because the next
pull from Transifex would automatically overwrite them again.

Branches
-------

### develop
The develop branch is typically used by developers as the main branch for integrating new features and changes into the codebase.
Pull requests should always be made to this branch (except for critical fixes), and might possibly break the code.
The develop branch is considered an unstable branch, as it is constantly updated with new code, and it may contain bugs or unfinished features. It is not guaranteed to work properly on any system.

### master
The master branch gets latest updates from the stable branch.
However, it may contain experimental features and should be used with caution.

### 13.2
The release branch for Blackcoin More 13.2.x. It is intended to contain stable and functional code that has been thoroughly tested and reviewed.

### 22.x
The release branch for Blackcoin More 22.x. Contains functional but experimental code.

### 23.x
The release branch for Blackcoin More 23.x. Contains functional but highly experimental code.
