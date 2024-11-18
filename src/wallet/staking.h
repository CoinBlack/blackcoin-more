// Copyright (c) 2014-2023 The Blackcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Staking start/stop algos by Qtum
// Copyright (c) 2016-2023 The Qtum developers

#ifndef BLACKCOIN_WALLET_STAKE_H
#define BLACKCOIN_WALLET_STAKE_H

#include <wallet/spend.h>
#include <wallet/wallet.h>

namespace wallet {
/* Start staking */
void StartStake(CWallet& wallet);

/* Stop staking */
void StopStake(CWallet& wallet);

uint64_t GetStakeWeight(const CWallet& wallet);
void AvailableCoinsForStaking(const CWallet& wallet,
                           std::vector<std::pair<const CWalletTx*, unsigned int> >& vCoins,
                           const CCoinControl* coinControl = nullptr,
                           const CoinFilterParams& params = {}) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);
bool SelectCoinsForStaking(const CWallet& wallet, CAmount& nTargetValue, std::set<std::pair<const CWalletTx *, unsigned int> > &setCoinsRet, CAmount& nValueRet);
bool CreateCoinStake(CWallet& wallet, unsigned int nBits, int64_t nSearchInterval, CMutableTransaction& tx, CAmount& nFees, CTxDestination destination); 

} // namespace wallet

#endif // BLACKCOIN_WALLET_STAKE_H
