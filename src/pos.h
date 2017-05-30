// Copyright (c) 2015-2016 The BlackCoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLACKCOIN_POS_H
#define BLACKCOIN_POS_H

#include "pos.h"
#include "txdb.h"
#include "main.h"
#include "arith_uint256.h"
#include "consensus/validation.h"
#include "hash.h"
#include "timedata.h"
#include "chainparams.h"
#include "script/sign.h"
#include <stdint.h>

using namespace std;

/** Compute the hash modifier for proof-of-stake */
uint256 ComputeStakeModifier(const CBlockIndex* pindexPrev, const uint256& kernel);

static const int nStakeMinConfirmations = 500;
static const unsigned int nStakeMinAge = 8 * 60 * 60; // 8 hours
// To decrease granularity of timestamp
// Supposed to be 2^n-1
static const int STAKE_TIMESTAMP_MASK = 15;

// Check whether the coinstake timestamp meets protocol
bool CheckCoinStakeTimestamp(int64_t nTimeBlock, int64_t nTimeTx);
bool CheckStakeBlockTimestamp(int64_t nTimeBlock);
bool CheckStakeKernelHash(const CBlockIndex* pindexPrev, unsigned int nBits, const CCoins* txPrev, const COutPoint& prevout, unsigned int nTimeTx);
bool IsConfirmedInNPrevBlocks(const CDiskTxPos& txindex, const CBlockIndex* pindexFrom, int nMaxDepth, int& nActualDepth);
bool CheckProofOfStake(CBlockIndex* pindexPrev, const CTransaction& tx, unsigned int nBits, CValidationState &state);
bool VerifySignature(const CTransaction& txFrom, const CTransaction& txTo, unsigned int nIn, unsigned int flags, int nHashType);
#endif // BLACKCOIN_POS_H
