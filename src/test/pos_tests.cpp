// Copyright (c) 2026 The Blackcoin More developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <coins.h>
#include <pos.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <map>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace {

// Minimal in-memory CCoinsView for testing.
class CoinsViewTest : public CCoinsView {
    std::map<COutPoint, Coin> m_coins;
    uint256 m_best_block;
public:
    bool GetCoin(const COutPoint& outpoint, Coin& coin) const override {
        auto it = m_coins.find(outpoint);
        if (it == m_coins.end()) return false;
        coin = it->second;
        return true;
    }
    bool HaveCoin(const COutPoint& outpoint) const override {
        return m_coins.count(outpoint);
    }
    uint256 GetBestBlock() const override { return m_best_block; }
    bool BatchWrite(CoinsViewCacheCursor& cursor, const uint256& hashBlock) override {
        m_best_block = hashBlock;
        for (auto* entry = cursor.Begin(); entry != cursor.End(); entry = cursor.NextAndMaybeErase(*entry)) {
            if (!entry->second.coin.IsSpent()) {
                m_coins[entry->first] = entry->second.coin;
            }
        }
        return true;
    }
    std::unique_ptr<CCoinsViewCursor> Cursor() const override { return {}; }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(pos_tests, RegTestingSetup)

// CheckKernel with stakecache must return the same result as without cache.
// The cache stores (blockFromTime, amount) per prevout. On a cache hit,
// CheckKernel double-checks by recursing without the cache to guard against
// stale entries on reorg.
BOOST_AUTO_TEST_CASE(checkkernel_cache_matches_no_cache)
{
    // Build a minimal chain: 12 blocks (maturity on regtest is 10).
    // Block 0 is genesis-like (height 0), blocks 1-11 follow.
    // The coin is created at height 1, pindexPrev is height 11.
    // Maturity check: pindexPrev->nHeight + 1 - coin.nHeight = 12 - 1 = 11 >= 10.
    const int chain_len = 12;
    std::vector<uint256> vHash(chain_len);
    std::vector<CBlockIndex> vBlocks(chain_len);
    for (int i = 0; i < chain_len; i++) {
        vHash[i] = ArithToUint256(arith_uint256(i + 1));
        vBlocks[i].nHeight = i;
        vBlocks[i].nTime = 1700000000 + i * 64;
        vBlocks[i].pprev = i ? &vBlocks[i - 1] : nullptr;
        vBlocks[i].phashBlock = &vHash[i];
        vBlocks[i].BuildSkip();
    }

    CBlockIndex* pindexPrev = &vBlocks[chain_len - 1];

    // Create a coin at height 1 with known nTime and value.
    CMutableTransaction tx;
    tx.version = 1;
    tx.nTime = vBlocks[1].nTime;
    tx.vout.emplace_back(100 * COIN, CScript() << OP_TRUE);
    const COutPoint prevout(tx.GetHash(), 0);

    // Set up a coins view with the coin.
    CoinsViewTest backend;
    CCoinsViewCache cache(&backend);
    cache.AddCoin(prevout, Coin(tx.vout[0], 1, false, false, (int)tx.nTime), false);
    cache.Flush();

    // Populate the stake cache.
    std::map<COutPoint, CStakeCache> stakeCache;
    CacheKernel(stakeCache, prevout, pindexPrev, cache);
    BOOST_CHECK_EQUAL(stakeCache.size(), 1);
    BOOST_CHECK_EQUAL(stakeCache.at(prevout).blockFromTime, (uint32_t)tx.nTime);
    BOOST_CHECK_EQUAL(stakeCache.at(prevout).amount, 100 * COIN);

    // CheckKernel with and without cache must return the same result.
    // Use a very high nBits (low difficulty) so the kernel hash likely passes.
    unsigned int nBits = 0x207fffff; // regtest powLimit — easiest possible target
    uint32_t nTime = pindexPrev->nTime + 64; // next valid window

    bool resultWithCache = CheckKernel(pindexPrev, nBits, nTime, prevout, cache, stakeCache);
    bool resultWithoutCache = CheckKernel(pindexPrev, nBits, nTime, prevout, cache);
    BOOST_CHECK_EQUAL(resultWithCache, resultWithoutCache);
}

// CacheKernel must use blockFrom->nTime when coin.nTime is 0 (v2 tx case).
BOOST_AUTO_TEST_CASE(cachekernel_uses_block_time_when_coin_ntime_zero)
{
    const int chain_len = 12;
    std::vector<uint256> vHash(chain_len);
    std::vector<CBlockIndex> vBlocks(chain_len);
    for (int i = 0; i < chain_len; i++) {
        vHash[i] = ArithToUint256(arith_uint256(i + 100));
        vBlocks[i].nHeight = i;
        vBlocks[i].nTime = 1700000000 + i * 64;
        vBlocks[i].pprev = i ? &vBlocks[i - 1] : nullptr;
        vBlocks[i].phashBlock = &vHash[i];
        vBlocks[i].BuildSkip();
    }

    CBlockIndex* pindexPrev = &vBlocks[chain_len - 1];

    // Create a coin at height 1 with nTime=0 (as v2 txs would have after deserialization).
    CMutableTransaction tx;
    tx.version = 2;
    tx.nTime = 0;
    tx.vout.emplace_back(50 * COIN, CScript() << OP_TRUE);
    const COutPoint prevout(tx.GetHash(), 0);

    CoinsViewTest backend;
    CCoinsViewCache cache(&backend);
    cache.AddCoin(prevout, Coin(tx.vout[0], 1, false, false, 0), false);
    cache.Flush();

    std::map<COutPoint, CStakeCache> stakeCache;
    CacheKernel(stakeCache, prevout, pindexPrev, cache);
    BOOST_CHECK_EQUAL(stakeCache.size(), 1);

    // When coin.nTime is 0, CacheKernel should fall back to blockFrom->nTime.
    BOOST_CHECK_EQUAL(stakeCache.at(prevout).blockFromTime, (uint32_t)vBlocks[1].nTime);
}

// Safety bump modulo 16000 strip: when MTP is inflated (e.g. by a remote node
// manipulating future timestamps), the raw sleep would be grossly inflated.
// The modulo 16000 strip preserves the true offset to the next 16-second boundary.
// This tests the arithmetic in isolation — the same logic appears in
// miner.cpp CreateNewBlock() and wallet.cpp updatedBlockTip().
BOOST_AUTO_TEST_CASE(safety_bump_modulo_strip_math)
{
    const int64_t WINDOW_MS = 16000; // (nStakeTimestampMask + 1) * 1000 = 16 * 1000

    // Normal case: sleep within one window, no strip needed.
    int64_t sleepMs = 8000;
    if (sleepMs > WINDOW_MS) {
        sleepMs %= WINDOW_MS;
        if (sleepMs == 0) sleepMs = WINDOW_MS;
    }
    BOOST_CHECK_EQUAL(sleepMs, 8000);

    // Inflated MTP case: sleep would be 32000ms (2 windows ahead).
    // Modulo strips it to 0, then the zero-guard sets it to 16000.
    sleepMs = 32000;
    if (sleepMs > WINDOW_MS) {
        sleepMs %= WINDOW_MS;
        if (sleepMs == 0) sleepMs = WINDOW_MS;
    }
    BOOST_CHECK_EQUAL(sleepMs, 16000);

    // Inflated MTP case: sleep would be 25000ms.
    // Modulo strips to 9000ms (the true offset to the next boundary).
    sleepMs = 25000;
    if (sleepMs > WINDOW_MS) {
        sleepMs %= WINDOW_MS;
        if (sleepMs == 0) sleepMs = WINDOW_MS;
    }
    BOOST_CHECK_EQUAL(sleepMs, 9000);

    // Edge: exactly 16000ms — not stripped (not > WINDOW_MS).
    sleepMs = 16000;
    if (sleepMs > WINDOW_MS) {
        sleepMs %= WINDOW_MS;
        if (sleepMs == 0) sleepMs = WINDOW_MS;
    }
    BOOST_CHECK_EQUAL(sleepMs, 16000);

    // Edge: 16001ms — stripped to 1ms.
    sleepMs = 16001;
    if (sleepMs > WINDOW_MS) {
        sleepMs %= WINDOW_MS;
        if (sleepMs == 0) sleepMs = WINDOW_MS;
    }
    BOOST_CHECK_EQUAL(sleepMs, 1);
}

// CreateCoinStake timestamp abort: when a kernel is found but txNew.nTime
// <= MTP, the function must return false. This tests the condition in
// isolation — the actual function requires a full wallet setup.
BOOST_AUTO_TEST_CASE(timestamp_abort_condition)
{
    // The abort condition is: txNew.nTime < pindexPrev->GetMedianTimePast() + 1
    // i.e. the coinstake timestamp must be strictly greater than MTP.

    const int64_t mtp = 1700000064; // a 16s boundary
    const int64_t mask = 0xf;       // nStakeTimestampMask

    // Case 1: nTime aligned to boundary, exactly MTP+1 — should pass (not abort).
    uint32_t nTime = (uint32_t)(mtp + 1);
    nTime &= ~mask; // align to 16s boundary
    // mtp is already aligned, so mtp+1 aligned down = mtp
    // nTime = mtp, which is < mtp+1 → would abort
    // This is the edge case the safety bump handles: nTime lands on MTP itself.
    BOOST_CHECK(nTime < mtp + 1); // abort condition true

    // Case 2: nTime at next boundary (mtp + 16) — should pass.
    nTime = (uint32_t)(mtp + 16);
    nTime &= ~mask;
    BOOST_CHECK(nTime >= mtp + 1); // abort condition false

    // Case 3: nTime at mtp + 32 — clearly past MTP.
    nTime = (uint32_t)(mtp + 32);
    nTime &= ~mask;
    BOOST_CHECK(nTime >= mtp + 1); // abort condition false
}

BOOST_AUTO_TEST_SUITE_END()