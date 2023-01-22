// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_POLICY_FEES_H
#define BITCOIN_POLICY_FEES_H

#include <consensus/amount.h>
#include <policy/feerate.h>
#include <uint256.h>
#include <random.h>
#include <sync.h>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

class CFeeRate;

class FeeFilterRounder
{
private:
    static constexpr double MAX_FILTER_FEERATE = 1e7;
    /** FEE_FILTER_SPACING is just used to provide some quantization of fee
     * filter results.  Historically it reused FEE_SPACING, but it is completely
     * unrelated, and was made a separate constant so the two concepts are not
     * tied together */
    static constexpr double FEE_FILTER_SPACING = 1.1;

public:
    /** Create new FeeFilterRounder */
    explicit FeeFilterRounder(const CFeeRate& minIncrementalFee);

    /** Quantize a minimum fee for privacy purpose before broadcast. Not thread-safe due to use of FastRandomContext */
    CAmount round(CAmount currentMinFee);

private:
    std::set<double> feeset;
    FastRandomContext insecure_rand;
};

#endif // BITCOIN_POLICY_FEES_H
