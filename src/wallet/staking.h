// Copyright (c) 2014-2023 The Blackcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Staking start/stop algos by Qtum
// Copyright (c) 2016-2023 The Qtum developers

#ifndef BLACKCOIN_WALLET_STAKE_H
#define BLACKCOIN_WALLET_STAKE_H

#include <wallet/wallet.h>

namespace wallet {
/* Start staking */
void StartStake(CWallet& wallet);

/* Stop staking */
void StopStake(CWallet& wallet);

} // namespace wallet

#endif // BLACKCOIN_WALLET_STAKE_H
