// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_POLICY_FEES_H
#define BITCOIN_POLICY_FEES_H

#include <consensus/amount.h>
#include <policy/feerate.h>
#include <random.h>
#include <sync.h>
#include <threadsafety.h>
#include <uint256.h>
#include <util/fs.h>

#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <set>
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
    explicit FeeFilterRounder(const CFeeRate& min_incremental_fee, FastRandomContext& rng);

    /** Quantize a minimum fee for privacy purpose before broadcast. */
    CAmount round(CAmount currentMinFee) EXCLUSIVE_LOCKS_REQUIRED(!m_insecure_rand_mutex);

private:
    const std::set<double> m_fee_set;
    Mutex m_insecure_rand_mutex;
    FastRandomContext& insecure_rand GUARDED_BY(m_insecure_rand_mutex);
};

#endif // BITCOIN_POLICY_FEES_H
