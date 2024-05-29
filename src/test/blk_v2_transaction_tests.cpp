// Copyright (c) 2018-2023 The Blackcoin More developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <coins.h>
#include <key.h>
#include <policy/policy.h>
#include <script/signingprovider.h>
#include <span.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <test/util/transaction_utils.h>
#include <util/strencodings.h>

static CFeeRate g_dust{DUST_RELAY_TX_FEE};
static bool g_bare_multi{DEFAULT_PERMIT_BAREMULTISIG};

BOOST_FIXTURE_TEST_SUITE(v2_transaction_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(IsStandard_test)
{
    SelectParams(ChainType::MAIN);

    FillableSigningProvider keystore;
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);
    std::vector<CMutableTransaction> dummyTransactions =
        SetupDummyInputs(keystore, coins, {11*CENT, 50*CENT, 21*CENT, 22*CENT});

    CMutableTransaction t;
    t.vin.resize(1);
    t.vin[0].prevout.hash = dummyTransactions[0].GetHash();
    t.vin[0].prevout.n = 1;
    t.vin[0].scriptSig << std::vector<unsigned char>(65, 0);
    t.vout.resize(1);
    t.vout[0].nValue = 90*CENT;
    CKey key;
    key.MakeNewKey(true);
    t.vout[0].scriptPubKey = GetScriptForDestination(PKHash(key.GetPubKey()));

    constexpr auto CheckIsStandard = [](const auto& t) {
        std::string reason;
        BOOST_CHECK(IsStandardTx(CTransaction{t}, MAX_OP_RETURN_RELAY, g_bare_multi, g_dust, reason));
        BOOST_CHECK(reason.empty());
    };
    constexpr auto CheckIsNotStandard = [](const auto& t, const std::string& reason_in) {
        std::string reason;
        BOOST_CHECK(!IsStandardTx(CTransaction{t}, MAX_OP_RETURN_RELAY, g_bare_multi, g_dust, reason));
        BOOST_CHECK_EQUAL(reason_in, reason);
    };

    CheckIsStandard(t);

    // Allowed nVersion
    t.nVersion = 1;
    CheckIsStandard(t);

    t.nVersion = 2;
    CheckIsStandard(t);

    // Disallowed nVersion
    t.nVersion = 3;
    CheckIsNotStandard(t, "version");

    // Allowed nVersion, empty nTime
    t.nVersion = 1;
    t.nTime = 0;
    CheckIsStandard(t);

    t.nVersion = 2;
    t.nTime = 0;
    CheckIsStandard(t);

    // Disallowed nVersion, empty nTime
    t.nVersion = 3;
    t.nTime = 0;
    CheckIsNotStandard(t, "version");

    // Check transaction version after V3_1 fork
    // Allowed nVersion, after-fork nTime
    t.nVersion = 1;
    t.nTime = Params().GetConsensus().nProtocolV3_1Time + 1;
    CheckIsStandard(t);

    t.nVersion = 2;
    t.nTime = Params().GetConsensus().nProtocolV3_1Time + 1;
    CheckIsStandard(t);

    // Disallowed nVersion, after-fork nTime
    t.nVersion = 3;
    t.nTime = Params().GetConsensus().nProtocolV3_1Time + 1;
    CheckIsNotStandard(t, "version");
}

BOOST_AUTO_TEST_SUITE_END()
