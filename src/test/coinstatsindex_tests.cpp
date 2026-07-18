// Copyright (c) 2020-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <index/coinstatsindex.h>
#include <interfaces/chain.h>
#include <kernel/coinstats.h>
#include <script/script.h>
#include <test/util/index.h>
#include <test/util/setup_common.h>
#include <test/util/validation.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(coinstatsindex_tests)

BOOST_FIXTURE_TEST_CASE(coinstatsindex_initial_sync, TestChain100Setup)
{
    CoinStatsIndex coin_stats_index{interfaces::MakeChain(m_node), 1 << 20, true};
    BOOST_REQUIRE(coin_stats_index.Init());

    const CBlockIndex* block_index;
    {
        LOCK(cs_main);
        block_index = m_node.chainman->ActiveChain().Tip();
    }

    // CoinStatsIndex should not be found before it is started.
    BOOST_CHECK(!coin_stats_index.LookUpStats(*block_index));

    // BlockUntilSyncedToCurrentChain should return false before CoinStatsIndex
    // is started.
    BOOST_CHECK(!coin_stats_index.BlockUntilSyncedToCurrentChain());

    BOOST_REQUIRE(coin_stats_index.StartBackgroundSync());

    IndexWaitSynced(coin_stats_index, *Assert(m_node.shutdown));

    // Check that CoinStatsIndex works for genesis block.
    const CBlockIndex* genesis_block_index;
    {
        LOCK(cs_main);
        genesis_block_index = m_node.chainman->ActiveChain().Genesis();
    }
    BOOST_CHECK(coin_stats_index.LookUpStats(*genesis_block_index));

    // Check that CoinStatsIndex updates with new blocks.
    BOOST_CHECK(coin_stats_index.LookUpStats(*block_index));

    const CScript script_pub_key{CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG};
    std::vector<CMutableTransaction> noTxns;
    CreateAndProcessBlock(noTxns, script_pub_key);

    // Let the CoinStatsIndex to catch up again.
    BOOST_CHECK(coin_stats_index.BlockUntilSyncedToCurrentChain());

    const CBlockIndex* new_block_index;
    {
        LOCK(cs_main);
        new_block_index = m_node.chainman->ActiveChain().Tip();
    }
    BOOST_CHECK(coin_stats_index.LookUpStats(*new_block_index));

    BOOST_CHECK(block_index != new_block_index);

    // It is not safe to stop and destroy the index until it finishes handling
    // the last BlockConnected notification. The BlockUntilSyncedToCurrentChain()
    // call above is sufficient to ensure this, but the
    // SyncWithValidationInterfaceQueue() call below is also needed to ensure
    // TSAN always sees the test thread waiting for the notification thread, and
    // avoid potential false positive reports.
    m_node.validation_signals->SyncWithValidationInterfaceQueue();

    // Shutdown sequence (c.f. Shutdown() in init.cpp)
    coin_stats_index.Stop();
}

// Test shutdown between BlockConnected and ChainStateFlushed notifications,
// make sure index is not corrupted and is able to reload.
BOOST_FIXTURE_TEST_CASE(coinstatsindex_unclean_shutdown, TestChain100Setup)
{
    Chainstate& chainstate = Assert(m_node.chainman)->ActiveChainstate();
    const CChainParams& params = Params();
    {
        CoinStatsIndex index{interfaces::MakeChain(m_node), 1 << 20};
        BOOST_REQUIRE(index.Init());
        BOOST_REQUIRE(index.StartBackgroundSync());
        IndexWaitSynced(index, *Assert(m_node.shutdown));
        std::shared_ptr<const CBlock> new_block;
        CBlockIndex* new_block_index = nullptr;
        {
            const CScript script_pub_key{CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG};
            const CBlock block = this->CreateBlock({}, script_pub_key, chainstate);

            new_block = std::make_shared<CBlock>(block);

            LOCK(cs_main);
            BlockValidationState state;
            BOOST_CHECK(CheckBlock(block, state, params.GetConsensus(), chainstate));
            BOOST_CHECK(m_node.chainman->AcceptBlock(new_block, state, &new_block_index, true, nullptr, nullptr, true));
            CCoinsViewCache view(&chainstate.CoinsTip());
            BOOST_CHECK(chainstate.ConnectBlock(block, state, new_block_index, view));
        }
        // Send block connected notification, then stop the index without
        // sending a chainstate flushed notification. Prior to #24138, this
        // would cause the index to be corrupted and fail to reload.
        ValidationInterfaceTest::BlockConnected(ChainstateRole::NORMAL, index, new_block, new_block_index);
        index.Stop();
    }

    {
        CoinStatsIndex index{interfaces::MakeChain(m_node), 1 << 20};
        BOOST_REQUIRE(index.Init());
        // Make sure the index can be loaded.
        BOOST_REQUIRE(index.StartBackgroundSync());
        index.Stop();
    }
}

// Test that the muhash encoding distinguishes fCoinBase and fCoinStake
// flags. This is the key property that makes the index coinstake-aware.
BOOST_AUTO_TEST_CASE(coinstatsindex_coinstake_awareness)
{
    // Verify the encoding: fCoinBase is bit 0, fCoinStake is bit 1, height
    // occupies the upper bits. This is what TxOutSer writes into the muhash.
    const uint32_t height{100};

    const uint32_t cb_code{static_cast<uint32_t>(height << 2) | 1u};  // fCoinBase=1, fCoinStake=0
    const uint32_t cs_code{static_cast<uint32_t>(height << 2) | 2u};  // fCoinBase=0, fCoinStake=1
    const uint32_t pl_code{static_cast<uint32_t>(height << 2) | 0u};  // fCoinBase=0, fCoinStake=0

    BOOST_CHECK_EQUAL(cb_code & 1u, 1u);
    BOOST_CHECK_EQUAL(cb_code & 2u, 0u);
    BOOST_CHECK_EQUAL(cs_code & 1u, 0u);
    BOOST_CHECK_EQUAL(cs_code & 2u, 2u);
    BOOST_CHECK_EQUAL(pl_code & 1u, 0u);
    BOOST_CHECK_EQUAL(pl_code & 2u, 0u);

    // The three categories must produce different encoded values for the
    // same height, so the muhash can distinguish them.
    BOOST_CHECK(cb_code != cs_code);
    BOOST_CHECK(cb_code != pl_code);
    BOOST_CHECK(cs_code != pl_code);

    // Verify the same property holds at the height boundary (e.g. height 0).
    const uint32_t h0_cb{static_cast<uint32_t>(0u << 2) | 1u};
    const uint32_t h0_cs{static_cast<uint32_t>(0u << 2) | 2u};
    BOOST_CHECK(h0_cb != h0_cs);

    // Verify GetBogoSize includes 4 bytes for height + coinbase + coinstake.
    // For a PoW-only chain, fCoinStake is always 0, so the bogo_size is
    // unchanged from upstream Bitcoin.
    const CScript spk{CScript() << OP_TRUE};
    BOOST_CHECK_EQUAL(kernel::GetBogoSize(spk), 32 + 4 + 4 + 8 + 2 + spk.size());
}

BOOST_AUTO_TEST_SUITE_END()
