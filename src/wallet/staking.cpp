// Copyright (c) 2014-2023 The Blackcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Staking start/stop algos by Qtum
// Copyright (c) 2016-2023 The Qtum developers

#include <wallet/staking.h>
#include <node/miner.h>

namespace wallet {

void StakeCoins(CWallet& wallet, bool fStake) {
    node::StakeCoins(fStake, &wallet, wallet.stakingThread);
}

void StartStake(CWallet& wallet) {
    if (!WITH_LOCK(wallet.cs_wallet, return wallet.GetKeyPoolSize())) {
        wallet.WalletLogPrintf("Error: Keypool is empty, please make sure the wallet contains keys and call keypoolrefill before restarting the staking thread\n");
        wallet.m_enabled_staking = false;
    }
    else if (wallet.IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        wallet.m_enabled_staking = false;
    }  
    else {
        wallet.m_enabled_staking = true;
    }
    StakeCoins(wallet, wallet.m_enabled_staking);
}

void StopStake(CWallet& wallet) {
    if (!wallet.stakingThread) {
        if (wallet.m_enabled_staking)
            wallet.m_enabled_staking = false;
    }
    else {
        wallet.m_stop_staking_thread = true;
        wallet.m_enabled_staking = false;
        StakeCoins(wallet, false);
        wallet.stakingThread = 0;
        wallet.m_stop_staking_thread = false;
    }
}
} // namespace wallet
