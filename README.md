Blackcoin Lore
=====================================

https://blackcoin.co

What is Blackcoin Lore?
----------------

Blackcoin is a decentralised digital currency with near-instant transaction speeds and
negligible transaction fees built upon Proof of Stake 3.0 as introduced by the Blackcoin development team. 

Lore takes Blackcoin to the next level by building upon Bitcoin Core 0.12 to offer performance enhancements,
wider compatibility with third party services and a more advanced base.

For downloads vist: https://github.com/janko33bd/bitcoin/releases

License
-------

Blackcoin Lore is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

Development Process
-------------------

The `Blackcoin-Lore` branch is regularly built and tested, but is not guaranteed to be
completely stable. [Tags](https://github.com/janko33bd/bitcoin/tags) are created
regularly to indicate new official, stable release versions of Blackcoin Lore.

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md).

The best place to get started is to join the Development channel on Gitter: https://gitter.im/Blackcoin\_Hub/Development

Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write [unit tests](/doc/unit-tests.md) for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run
(assuming they weren't disabled in configure) with: `make check`

There are also [regression and integration tests](/qa) of the RPC interface, written
in Python, that are run automatically on the build server.
These tests can be run with: `qa/pull-tester/rpc-tests.py`

### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than the developer who wrote the
code. This is especially important for large or high-risk changes. It is useful
to add a test plan to the pull request description if testing the changes is
not straightforward.
