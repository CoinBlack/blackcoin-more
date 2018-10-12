// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
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
    unsigned int nTargetLimit = UintToArith256(params.powLimit).GetCompact();

        // Genesis block
        if (pindexLast == NULL)
            return nTargetLimit;

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

    int64_t nActualSpacing = pindexLast->GetBlockTime() - nFirstBlockTime;
    int64_t nTargetSpacing = params.IsProtocolV2(pindexLast->GetBlockTime()) ? params.nTargetSpacing : params.nTargetSpacingV1;

    // Limit adjustment step
    if (pindexLast->GetBlockTime() > params.nProtocolV1RetargetingFixedTime && nActualSpacing < 0)
        nActualSpacing = nTargetSpacing;
    if (pindexLast->GetBlockTime() > params.nProtocolV3Time && nActualSpacing > nTargetSpacing*10)
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
        return error("CheckProofOfWork(): nBits below minimum work");

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return error("CheckProofOfWork(): hash doesn't match nBits");

    return true;
}

arith_uint256 GetBlockProof(const CBlockIndex& block)
{
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a arith_uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}

int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params& params)
{
    arith_uint256 r;
    int sign = 1;
    if (to.nChainWork > from.nChainWork) {
        r = to.nChainWork - from.nChainWork;
    } else {
        r = from.nChainWork - to.nChainWork;
        sign = -1;
    }
    r = r * arith_uint256(params.nTargetSpacing) / GetBlockProof(tip);
    if (r.bits() > 63) {
        return sign * std::numeric_limits<int64_t>::max();
    }
    return sign * r.GetLow64();
}
