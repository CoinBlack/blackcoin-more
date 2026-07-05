// Copyright (c) 2020-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INDEX_COINSTATSINDEX_H
#define BITCOIN_INDEX_COINSTATSINDEX_H

#include <crypto/muhash.h>
#include <index/base.h>

class CBlockIndex;
class CDBBatch;
namespace kernel {
struct CCoinsStats;
}

static constexpr bool DEFAULT_COINSTATSINDEX{false};

/**
 * CoinStatsIndex maintains statistics on the UTXO set.
 */
class CoinStatsIndex final : public BaseIndex
{
private:
    std::unique_ptr<BaseIndex::DB> m_db;

    MuHash3072 m_muhash;
    uint64_t m_transaction_output_count{0};
    uint64_t m_bogo_size{0};
    CAmount m_total_amount{0};
    // Blackcoin: tracks the total of (PoW coinbase subsidy + PoS stake reward)
    // across all blocks. The name "subsidy" is inherited from upstream where it
    // refers to the PoW coinbase only. On Blackcoin's hybrid chain it covers
    // both. Not renamed for forward-port compatibility.
    CAmount m_total_subsidy{0};
    CAmount m_total_unspendable_amount{0};
    CAmount m_total_prevout_spent_amount{0};
    CAmount m_total_new_outputs_ex_coinbase_amount{0};
    CAmount m_total_coinbase_amount{0};
    // Blackcoin: BIP30-unspendable tracking is intentionally not present.
    // The consensus rule is still enforced (see validation.cpp), but no
    // Blackcoin block is BIP30-vulnerable, so the accounting channel is
    // always zero. Do not re-add when forward-porting from upstream unless
    // Blackcoin's chain history changes to include a BIP30 collision.
    CAmount m_total_unspendables_genesis_block{0};
    CAmount m_total_unspendables_scripts{0};
    CAmount m_total_unspendables_unclaimed_rewards{0};

    [[nodiscard]] bool ReverseBlock(const CBlock& block, const CBlockIndex* pindex);

    /*
    // Blackcoin: pruning is not allowed
    bool AllowPrune() const override { return true; }
    */

protected:
    bool CustomInit(const std::optional<interfaces::BlockKey>& block) override;

    bool CustomCommit(CDBBatch& batch) override;

    bool CustomAppend(const interfaces::BlockInfo& block) override;

    bool CustomRewind(const interfaces::BlockKey& current_tip, const interfaces::BlockKey& new_tip) override;

    BaseIndex::DB& GetDB() const override { return *m_db; }

public:
    // Constructs the index, which becomes available to be queried.
    explicit CoinStatsIndex(std::unique_ptr<interfaces::Chain> chain, size_t n_cache_size, bool f_memory = false, bool f_wipe = false);

    // Look up stats for a specific block using CBlockIndex
    std::optional<kernel::CCoinsStats> LookUpStats(const CBlockIndex& block_index) const;
};

/// The global UTXO set hash object.
extern std::unique_ptr<CoinStatsIndex> g_coin_stats_index;

#endif // BITCOIN_INDEX_COINSTATSINDEX_H
