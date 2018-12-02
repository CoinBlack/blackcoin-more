# Changelog

## v2.13.2.1 (2018-12-02)

- Updated to Bitcoin Core 0.13.2
- Some small fixes and updates from Bitcoin Core 0.14.x branch
- Fixed testnet and regtest
- Added Qt 5.9 support for cross-compile
- Added Qt support for ARMv7
- Added out-of-sync modal window (backport of Core's PR8371, PR8802, PR8805, PR8906, PR8985, PR9088, PR9461, PR9462)
- Added support for nested commands and simple value queries in RPC console (backport of Core's PR7783)
- Added abortrescan RPC call (backport of Core's PR10208)
- Added reservebalance RPC call
- Removed SegWit
- Removed replace-by-fee
- Removed address indexes
- Removed relaying of double-spends
- Removed drivechain support using OP_COUNT_ACKS
- Proof-of-stake related code optimized and refactored

## v2.12.1.1 (2018-10-01)

- Rebranded to Blackcoin More
- Some small fixes and updates from Bitcoin Core 0.13.x branch
- Added use available balance button in send coins dialog (backport of Core's PR11316)
- Added a button to open the config file in a text editor (backport of Core's PR9890)
- Added uptime RPC call (backport of Core's PR10400)
- Removed P2P alert system (backport of Core's PR7692)
