// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"
#include <stdio.h>

static arith_uint256 GetTargetLimit(int64_t nTime, const Consensus::Params& params, bool fProofOfStake)
{
    uint256 nLimit;

    if (fProofOfStake) {
        if (params.IsProtocolV2(nTime))
            nLimit = params.posLimitV2;
        else
            nLimit = params.posLimit;
    } else {
        nLimit = params.powLimit;
    }

    return UintToArith256(nLimit);
}

unsigned int GetNextTargetRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params, bool fProofOfStake)
{
    unsigned int nTargetLimit = GetTargetLimit(pindexLast->GetBlockTime(), params, fProofOfStake).GetCompact();

    // Genesis block
    if (pindexLast == NULL)
        return UintToArith256(params.powLimit).GetCompact();

    const CBlockIndex* pindexPrev = GetLastBlockIndex(pindexLast, fProofOfStake);

    if (pindexPrev->pprev == NULL)
        return nTargetLimit; // first block
    const CBlockIndex* pindexPrevPrev = GetLastBlockIndex(pindexPrev->pprev, fProofOfStake);
    if (pindexPrevPrev->pprev == NULL)
        return nTargetLimit; // second block

    return CalculateNextTargetRequired(pindexPrev, pindexPrevPrev->GetBlockTime(), params, fProofOfStake);
}

unsigned int CalculateNextTargetRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params, bool fProofOfStake)
{
    if (fProofOfStake) {
        if (params.fPoSNoRetargeting)
            return pindexLast->nBits;
    } else {
        if (params.fPowNoRetargeting)
            return pindexLast->nBits;
    }

    int64_t nTargetSpacing = params.IsProtocolV2(pindexLast->GetBlockTime()) ? params.nTargetSpacing : params.nTargetSpacingV1;
    int64_t nActualSpacing = pindexLast->GetBlockTime() - nFirstBlockTime;

    // Limit adjustment step
    if (params.IsProtocolV1RetargetingFixed(pindexLast->GetBlockTime()) && nActualSpacing < 0)
        nActualSpacing = nTargetSpacing;
    if (params.IsProtocolV3(pindexLast->GetBlockTime()) && nActualSpacing > nTargetSpacing*10)
        nActualSpacing = nTargetSpacing*10;

    // retarget with exponential moving toward target spacing
    const arith_uint256 bnTargetLimit = GetTargetLimit(pindexLast->GetBlockTime(), params, fProofOfStake);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    int64_t nInterval = params.nTargetTimespan / nTargetSpacing;
    bnNew *= ((nInterval - 1) * nTargetSpacing + nActualSpacing + nActualSpacing);
    bnNew /= ((nInterval + 1) * nTargetSpacing);

    if (bnNew <= 0 || bnNew > bnTargetLimit)
        bnNew = bnTargetLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
