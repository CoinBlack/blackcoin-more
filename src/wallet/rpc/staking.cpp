// Copyright (c) 2014-2023 The Blackcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-lice

#include <rpc/util.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>
#include <wallet/rpc/util.h>
#include <wallet/rpc/staking.h>
#include <wallet/wallet.h>
#include <node/miner.h>
#include <warnings.h>

#include <univalue.h>

using node::BlockAssembler;

namespace wallet {

static RPCHelpMan getstakinginfo()
{
    return RPCHelpMan{"getstakinginfo",
                "\nReturns an object containing staking-related information.",
                {},
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::BOOL, "enabled", "'true' if staking is enabled"},
                        {RPCResult::Type::BOOL, "staking", "'true' if wallet is currently staking"},
                        {RPCResult::Type::STR, "errors", "error messages"},
                        {RPCResult::Type::NUM, "pooledtx", "The size of the mempool"},
                        {RPCResult::Type::NUM, "difficulty", "The current difficulty"},
                        {RPCResult::Type::NUM, "search-interval", "The staker search interval"},
                        {RPCResult::Type::NUM, "weight", "The staker weight"},
                        {RPCResult::Type::NUM, "netstakeweight", "Network stake weight"},
                        {RPCResult::Type::NUM, "expectedtime", "Expected time to earn reward"},
                    }
                },
                RPCExamples{
                    HelpExampleCli("getstakinginfo", "")
            + HelpExampleRpc("getstakinginfo", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return NullUniValue;

    uint64_t nWeight = 0;

    if (pwallet)
    {
        LOCK(pwallet->cs_wallet);
        nWeight = pwallet->GetStakeWeight();
    }

    const CTxMemPool& mempool = pwallet->chain().mempool();
    ChainstateManager& chainman = pwallet->chain().chainman();

    LOCK(cs_main);
    const CChain& active_chain = chainman.ActiveChain();

    uint64_t nNetworkWeight = 1.1429 * GetPoSKernelPS();
    bool staking = nLastCoinStakeSearchInterval && nWeight;

    const Consensus::Params& consensusParams = Params().GetConsensus();
    int64_t nTargetSpacing = consensusParams.nTargetSpacing;
    uint64_t nExpectedTime = staking ? 1.0455 * nTargetSpacing * nNetworkWeight / nWeight : 0;

    UniValue obj(UniValue::VOBJ);

    obj.pushKV("enabled", node::EnableStaking());
    obj.pushKV("staking", staking);

    obj.pushKV("blocks", active_chain.Height());
    if (BlockAssembler::m_last_block_weight) obj.pushKV("currentblockweight", *BlockAssembler::m_last_block_weight);
    if (BlockAssembler::m_last_block_num_txs) obj.pushKV("currentblocktx", *BlockAssembler::m_last_block_num_txs);
    obj.pushKV("pooledtx", (uint64_t)mempool.size());

    obj.pushKV("difficulty", GetDifficulty(GetLastBlockIndex(pindexBestHeader, true)));

    obj.pushKV("search-interval", (uint64_t)nLastCoinStakeSearchInterval);
    obj.pushKV("weight", (uint64_t)nWeight);
    obj.pushKV("netstakeweight", (uint64_t)nNetworkWeight);
    obj.pushKV("expectedtime", nExpectedTime);

    obj.pushKV("chain", Params().NetworkIDString());
    obj.pushKV("warnings", GetWarnings(false).original);
    return obj;
},
    };
}

Span<const CRPCCommand> GetStakingRPCCommands()
{
// clang-format off
static const CRPCCommand commands[] =
{ //  category              actor (function)
  //  ------------------    ------------------------
    { "staking",            &getstakinginfo,                 },
};
// clang-format on
    return commands;
}

} // namespace wallet
