// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MINER_H
#define BITCOIN_MINER_H

#include "primitives/block.h"

#include <stdint.h>

class CBlockIndex;
class CChainParams;
class CReserveKey;
class CScript;
class CWallet;
class CBlock;
namespace Consensus { struct Params; };

static const bool DEFAULT_GENERATE = false;
static const int DEFAULT_GENERATE_THREADS = 1;

static const bool DEFAULT_PRINTPRIORITY = false;

struct CBlockTemplate
{
    CBlock block;
    std::vector<CAmount> vTxFees;
    std::vector<int64_t> vTxSigOps;
};

/** Run the miner threads */
void GenerateBitcoins(bool fGenerate, int nThreads, const CChainParams& chainparams);
void ThreadStakeMiner(CWallet *pwallet, const CChainParams& chainparams);
/** Run the miner threads */
bool SignBlock(CBlock& block, CWallet& wallet, int64_t& nFees);
/** Generate a new block, without valid proof-of-work */
CBlockTemplate* CreateNewBlock(const CChainParams& chainparams, const CScript& scriptPubKeyIn, int64_t* nFees = 0, bool fProofOfStake = false);
/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce);
int64_t UpdateTime(CBlock* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev);
bool CheckStake(CBlock* pblock, CWallet& wallet, const CChainParams& chainparams);
CAmount GetProofOfWorkReward();
#endif // BITCOIN_MINER_H
