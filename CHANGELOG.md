# Changelog
## v2.13.2.8 (2021-02-24)
- Immediately ban clients operating on forked chains older than nMaxReorganizationDepth
- Fixed IsDust() policy to allow atomic swaps
- Updated fixed seeds for mainnet and testnet
- Updated dependencies for MacOS

## v2.13.2.7 (2020-11-24)
- Dust mitigation in mempool (by JJ12880 from Radium Core) 
- Compile on MacOS Catalina
- Cross-compile MacOS with Xcode 11.3.1
- Updated dependencies for Windows x64, Linux x64, MacOS, ARM64, ARMv7
- Sign/verify compatibility with legacy clients 
- Increased dbcache to 450MB
- Disabled stake cache for now
- Updated fixed seeds for mainnet and testnet

## v2.13.2.6 (2020-07-21)
- Fix staking memory leak (by JJ12880 from Radium Core)
- Updated fixed seeds
- Added secondary Blackcoin DNS seeder

## v2.13.2.5 (2020-04-28)
- Updated Berkeley DB to 6.2.38
- Updated OpenSSL to 1.0.2u
- Updated fixed seeds
- Changed default port on regtest to 35714

## v2.13.2.4 (2019-11-11)
- Updated fixed seeds
- Added burn RPC call
- Set default MAX_OP_RETURN_RELAY to 15000
- Removed unit selector from status bar

## v2.13.2.3 (2019-04-02)
- Updated fixed seeds
- Some small fixes and refactorings
- Fixed wrongly displayed balances in GUI and RPC
- Added header spam filter (fake stake vulnerability fix)
- Added total balance in RPC call getwalletinfo

## v2.13.2.2 (2019-03-13)
- Updated dependencies
- Updated fixed seeds
- Some small fixes and updates
- Fixed walletpassphrase RPC call (wallet now can be unlocked for staking only)
- Allowed connections from peers with protocol version 60016
- Disabled BIP 152

## v2.13.2.1 (2018-12-03)

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
