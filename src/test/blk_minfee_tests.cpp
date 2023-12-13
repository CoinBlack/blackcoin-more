// Copyright (c) 2018-2023 The Blackcoin More developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/tx_verify.h>

BOOST_AUTO_TEST_SUITE(minfee_tests)

BOOST_AUTO_TEST_CASE(minfee_test)
{
    SelectParams(ChainType::MAIN);

    // Check minimum fees before V3_1 fork
    BOOST_CHECK_EQUAL(GetMinFee(0, 0), 0);
    BOOST_CHECK_EQUAL(GetMinFee(99, 0), 990);
    BOOST_CHECK_EQUAL(GetMinFee(100, 0), 1000);
    BOOST_CHECK_EQUAL(GetMinFee(101, 0), 1010);
    BOOST_CHECK_EQUAL(GetMinFee(10000, 0), 100000);

    BOOST_CHECK(GetMinFee(std::numeric_limits<size_t>::max(), 0) <= MAX_MONEY);

    // Check minimum fees after V3_1 fork
    BOOST_CHECK_EQUAL(GetMinFee(0, Params().GetConsensus().nProtocolV3_1Time + 1), 10000);
    BOOST_CHECK_EQUAL(GetMinFee(99, Params().GetConsensus().nProtocolV3_1Time + 1), 10000);
    BOOST_CHECK_EQUAL(GetMinFee(100, Params().GetConsensus().nProtocolV3_1Time + 1), 10000);
    BOOST_CHECK_EQUAL(GetMinFee(101, Params().GetConsensus().nProtocolV3_1Time + 1), 10100);
    BOOST_CHECK_EQUAL(GetMinFee(10000, Params().GetConsensus().nProtocolV3_1Time + 1), 1000000);

    BOOST_CHECK(GetMinFee(std::numeric_limits<size_t>::max(), Params().GetConsensus().nProtocolV3_1Time + 1) <= MAX_MONEY);
}

BOOST_AUTO_TEST_SUITE_END()
