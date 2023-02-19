// Copyright (c) 2014-2023 The Blackcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-li

#ifndef BLACKCOIN_WALLET_RPC_STAKING_H
#define BLACKCOIN_WALLET_RPC_STAKING_H

#include <span.h>

class CRPCCommand;

namespace wallet {
Span<const CRPCCommand> GetStakingRPCCommands();
} // namespace wallet

#endif // BLACKCOIN_WALLET_RPC_STAKING_H
