// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <uint256.h>
#include <map>
#include <string>

namespace Consensus {

enum DeploymentPos
{
    DEPLOYMENT_TESTDUMMY,
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in versionbits.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit;
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime;
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nMaxReorganizationDepth;
    /** Used to check majorities for block version upgrade */
    int nMajorityEnforceBlockUpgrade;
    int nMajorityRejectBlockOutdated;
    int nMajorityWindow;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargetting period,
     * (nTargetTimespan / nTargetSpacing) which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint256 powLimit;
    uint256 posLimit;
    uint256 posLimitV2;
    bool fPowAllowMinDifficultyBlocks;
    int64_t nTargetSpacingV1;
    bool fPowNoRetargeting;
    bool fPoSNoRetargeting;
    int64_t nTargetSpacing;
    int64_t nTargetTimespan;
    int64_t DifficultyAdjustmentInterval() const { return nTargetTimespan / nTargetSpacing; }
    int64_t nProtocolV1RetargetingFixedTime;
    int64_t nProtocolV2Time;
    int64_t nProtocolV3Time;
    int64_t nProtocolV3_1Time;
    bool IsProtocolV1RetargetingFixed(int64_t nTime) const { return nTime > nProtocolV1RetargetingFixedTime && nTime != 1395631999; }
    bool IsProtocolV2(int64_t nTime) const { return nTime > nProtocolV2Time && nTime != 1407053678; }
    bool IsProtocolV3(int64_t nTime) const { return nTime > nProtocolV3Time && nTime != 1444028400; }
    bool IsProtocolV3_1(int64_t nTime) const { return nTime > nProtocolV3_1Time && nTime != 1667779200; }
    unsigned int GetTargetSpacing(int nHeight) { return IsProtocolV2(nHeight) ? 64 : 60; }
    int nLastPOWBlock;
    int nStakeTimestampMask;
    int nCoinbaseMaturity;
    uint256 nMinimumChainWork;
};
} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
