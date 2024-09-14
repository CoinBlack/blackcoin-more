// Copyright (c) 2014-2023 The Blackcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Staking start/stop algos by Qtum
// Copyright (c) 2016-2023 The Qtum developers

#include <index/txindex.h>
#include <wallet/coincontrol.h>
#include <wallet/receive.h>
#include <wallet/staking.h>
#include <node/miner.h>

namespace wallet {

static int64_t GetStakeCombineThreshold() { return 500 * COIN; }
static int64_t GetStakeSplitThreshold() { return 2 * GetStakeCombineThreshold(); }

void StakeCoins(CWallet& wallet, bool fStake) {
    node::StakeCoins(fStake, &wallet, wallet.threadStakeMinerGroup);
}

void StartStake(CWallet& wallet) {
    if (wallet.IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        wallet.WalletLogPrintf("Wallet can't contain any private keys - staking disabled\n");
        wallet.m_enabled_staking = false;
    }
    else if (wallet.IsWalletFlagSet(WALLET_FLAG_BLANK_WALLET)) {
        wallet.WalletLogPrintf("Wallet is blank - staking disabled\n");
        wallet.m_enabled_staking = false;
    }
    else if (!WITH_LOCK(wallet.cs_wallet, return wallet.GetKeyPoolSize())) {
        wallet.WalletLogPrintf("Error: Keypool is empty, please make sure the wallet contains keys, call keypoolrefill and restart the staking thread\n");
        wallet.m_enabled_staking = false;
    }
    else {
        wallet.m_enabled_staking = true;
    }
    StakeCoins(wallet, wallet.m_enabled_staking);
}

void StopStake(CWallet& wallet) {
    if (!wallet.threadStakeMinerGroup) {
        if (wallet.m_enabled_staking)
            wallet.m_enabled_staking = false;
    }
    else {
        wallet.m_stop_staking_thread = true;
        wallet.m_enabled_staking = false;
        StakeCoins(wallet, false);
        wallet.threadStakeMinerGroup = 0;
        wallet.m_stop_staking_thread = false;
    }
}

uint64_t GetStakeWeight(const CWallet& wallet)
{
    // Choose coins to use
    const auto bal = GetBalance(wallet);
    CAmount nBalance = bal.m_mine_trusted;
    if (wallet.IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS))
        nBalance += bal.m_watchonly_trusted;

    if (nBalance <= wallet.m_reserve_balance)
        return 0;

    std::set<std::pair<const CWalletTx*, unsigned int> > setCoins;
    CAmount nValueIn = 0;

    CAmount nTargetValue = nBalance - wallet.m_reserve_balance;
    if (!SelectCoinsForStaking(wallet, nTargetValue, setCoins, nValueIn))
        return 0;

    if (setCoins.empty())
        return 0;

    uint64_t nWeight = 0;

    for (std::pair<const CWalletTx*,unsigned int> pcoin : setCoins)
    {
        if (wallet.GetTxDepthInMainChain(*pcoin.first) >= Params().GetConsensus().nCoinbaseMaturity)
        {
            nWeight += pcoin.first->tx->vout[pcoin.second].nValue;
        }
    }

    return nWeight;
}

void AvailableCoinsForStaking(const CWallet& wallet,
                           std::vector<std::pair<const CWalletTx*, unsigned int> >& vCoins,
                           const CCoinControl* coinControl,
                           const CoinFilterParams& params)
{
    AssertLockHeld(wallet.cs_wallet);

    vCoins.clear();
    CAmount nTotal = 0;
    // Either the WALLET_FLAG_AVOID_REUSE flag is not set (in which case we always allow), or we default to avoiding, and only in the case where
    // a coin control object is provided, and has the avoid address reuse flag set to false, do we allow already used addresses
    bool allow_used_addresses = !wallet.IsWalletFlagSet(WALLET_FLAG_AVOID_REUSE);
    const int min_depth = std::max(DEFAULT_MIN_DEPTH, Params().GetConsensus().nCoinbaseMaturity);
    const int max_depth = DEFAULT_MAX_DEPTH;
    const bool only_safe = true;

    std::set<uint256> trusted_parents;
    for (const auto& entry : wallet.mapWallet)
    {
        const uint256& wtxid = entry.first;
        const CWalletTx& wtx = entry.second;

        if (wallet.IsTxImmature(wtx))
            continue;

        int nDepth = wallet.GetTxDepthInMainChain(wtx);
        if (nDepth < 0)
            continue;

        // We should not consider coins which aren't at least in our mempool
        // It's possible for these to be conflicted via ancestors which we may never be able to detect
        if (nDepth == 0 && !wtx.InMempool())
            continue;

        bool safeTx = CachedTxIsTrusted(wallet, wtx, trusted_parents);

        if (only_safe && !safeTx) {
            continue;
        }

        if (nDepth < min_depth || nDepth > max_depth) {
            continue;
        }

        for (unsigned int i = 0; i < wtx.tx->vout.size(); i++) {
            const CTxOut& output = wtx.tx->vout[i];
            const COutPoint outpoint(wtxid, i);

            if (output.nValue < wallet.m_min_staking_amount)
                continue;

            if (output.nValue < params.min_amount || output.nValue > params.max_amount)
                continue;

            if (wallet.IsLockedCoin(outpoint) && params.skip_locked)
                continue;

            if (wallet.IsSpent(outpoint))
                continue;

            isminetype mine = wallet.IsMine(output);

            if (mine == ISMINE_NO) {
                continue;
            }

            if (!allow_used_addresses && wallet.IsSpentKey(output.scriptPubKey)) {
                continue;
            }

            std::unique_ptr<SigningProvider> provider = wallet.GetSolvingProvider(output.scriptPubKey);

            bool solvable = provider ? InferDescriptor(output.scriptPubKey, *provider)->IsSolvable() : false;
            bool spendable = ((mine & ISMINE_SPENDABLE) != ISMINE_NO) || (((mine & ISMINE_WATCH_ONLY) != ISMINE_NO) && (coinControl && coinControl->fAllowWatchOnly && solvable));

            // Filter by spendable outputs only
            if (!spendable && params.only_spendable) continue;

            // If the Output is P2SH and spendable, we want to know if it is
            // a P2SH (legacy) or one of P2SH-P2WPKH, P2SH-P2WSH (P2SH-Segwit). We can determine
            // this from the redeemScript. If the Output is not spendable, it will be classified
            // as a P2SH (legacy), since we have no way of knowing otherwise without the redeemScript
            CScript script;
            if (output.scriptPubKey.IsPayToScriptHash() && solvable) {
                CTxDestination destination;
                if (!ExtractDestination(output.scriptPubKey, destination))
                    continue;
                const CScriptID& hash = ToScriptID(std::get<ScriptHash>(destination));
                if (!provider->GetCScript(hash, script))
                    continue;
            } else {
                script = output.scriptPubKey;
            }

            if (spendable)
                vCoins.push_back(std::make_pair(&wtx, i));

            // Cache total amount as we go
            nTotal += output.nValue;
            // Checks the sum amount of all UTXO's.
            if (params.min_sum_amount != MAX_MONEY) {
                if (nTotal >= params.min_sum_amount) {
                    return;
                }
            }

            // Checks the maximum number of UTXO's.
            if (params.max_count > 0 && vCoins.size() >= params.max_count) {
                return;
            }
        }
    }
}

// Select some coins without random shuffle or best subset approximation
bool SelectCoinsForStaking(const CWallet& wallet, CAmount& nTargetValue, std::set<std::pair<const CWalletTx *, unsigned int> > &setCoinsRet, CAmount& nValueRet)
{
    std::vector<std::pair<const CWalletTx*, unsigned int> > vCoins;
    CCoinControl coincontrol;
    AvailableCoinsForStaking(wallet, vCoins, &coincontrol);

    setCoinsRet.clear();
    nValueRet = 0;

    for (const std::pair<const CWalletTx*, unsigned int> &output : vCoins)
    {

        const CWalletTx *pcoin = output.first;
        int i = output.second;

        // Stop if we've chosen enough inputs
        if (nValueRet >= nTargetValue)
            break;

        int64_t n = pcoin->tx->vout[i].nValue;

        std::pair<int64_t,std::pair<const CWalletTx*,unsigned int> > coin = std::make_pair(n,std::make_pair(pcoin, i));

        if (n >= nTargetValue)
        {
            // If input value is greater or equal to target then simply insert
            // it into the current subset and exit
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
            break;
        }
        else if (n < nTargetValue + CENT)
        {
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
        }
    }

    return true;
}

// peercoin: create coin stake transaction
typedef std::vector<unsigned char> valtype;
bool CreateCoinStake(CWallet& wallet, unsigned int nBits, int64_t nSearchInterval, CMutableTransaction& txNew, CAmount& nFees, CTxDestination destination)
{
    bool fAllowWatchOnly = wallet.IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
    CBlockIndex* pindexPrev = wallet.chain().getTip();
    arith_uint256 bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);

    // Transaction index is required to get to block header
    if (!g_txindex)
        return error("CreateCoinStake : transaction index unavailable");

    LOCK2(cs_main, wallet.cs_wallet);
    txNew.vin.clear();
    txNew.vout.clear();

    // Mark coin stake transaction
    CScript scriptEmpty;
    scriptEmpty.clear();
    txNew.vout.push_back(CTxOut(0, scriptEmpty));

    // Choose coins to use
    const auto bal = GetBalance(wallet);
    CAmount nBalance = bal.m_mine_trusted;
    if (fAllowWatchOnly)
        nBalance += bal.m_watchonly_trusted;

    if (nBalance <= wallet.m_reserve_balance)
        return false;

    std::set<std::pair<const CWalletTx*, unsigned int> > setCoins;
    std::vector<CTransactionRef> vwtxPrev;
    CAmount nValueIn = 0;
    CAmount nAllowedBalance = nBalance - wallet.m_reserve_balance;

    // Select coins with suitable depth
    if (!SelectCoinsForStaking(wallet, nAllowedBalance, setCoins, nValueIn))
        return false;

    if (setCoins.empty())
        return false;

    CAmount nCredit = 0;
    bool fKernelFound = false;
    CScript scriptPubKeyKernel, scriptPubKeyOut;
    bool bMinterKey = false;

    for (const std::pair<const CWalletTx*, unsigned int> &pcoin : setCoins)
    {
        uint256 blockHash;
        CTransactionRef tx;
        if (!g_txindex->FindTx(pcoin.first->GetHash(), blockHash, tx)) {
            LogPrintf("couldnt retrieve tx %s\n", *pcoin.first->GetHash().ToString().c_str());
            continue;
        }

        static int nMaxStakeSearchInterval = 60;
        for (unsigned int n=0; n<std::min(nSearchInterval,(int64_t)nMaxStakeSearchInterval) && !fKernelFound; n++)
        {
            // Search backward in time from the given txNew timestamp
            // Search nSearchInterval seconds back up to nMaxStakeSearchInterval
            COutPoint prevoutStake = COutPoint(pcoin.first->GetHash(), pcoin.second);
            if (CheckKernel(pindexPrev, nBits, txNew.nTime - n, prevoutStake, wallet.chain().getCoinsTip()))
            {
                // Found a kernel
                LogPrint(BCLog::COINSTAKE, "CreateCoinStake : kernel found\n");
                std::vector<valtype> vSolutions;
                scriptPubKeyKernel = pcoin.first->tx->vout[pcoin.second].scriptPubKey;
                TxoutType whichType = Solver(scriptPubKeyKernel, vSolutions);

                if (whichType != TxoutType::PUBKEY && whichType != TxoutType::PUBKEYHASH && whichType != TxoutType::WITNESS_V0_KEYHASH && whichType != TxoutType::WITNESS_V1_TAPROOT)
                {
                    LogPrint(BCLog::COINSTAKE, "CreateCoinStake : no support for kernel type=%s\n", GetTxnOutputType(whichType));
                    break;  // only support pay to public key and pay to address and pay to witness keyhash
                }
                if (whichType == TxoutType::PUBKEYHASH) // pay to address
                {
                    // convert to pay to public key type
                    CKey key;
                    if (wallet.IsLegacy()) {
                        auto scriptPubKeyMan = wallet.GetLegacyScriptPubKeyMan();
                        if (!scriptPubKeyMan) {
                            LogPrint(BCLog::COINSTAKE, "CreateCoinStake : failed to get scriptpubkeyman for kernel type=%s\n", GetTxnOutputType(whichType));
                            break;  // unable to find corresponding public key
                        }
                        if (!scriptPubKeyMan->GetKey(CKeyID(uint160(vSolutions[0])), key))
                        {
                            LogPrint(BCLog::COINSTAKE, "CreateCoinStake : failed to get key for kernel type=%s\n", GetTxnOutputType(whichType));
                            break;  // unable to find corresponding public key
                        }
                        scriptPubKeyOut << ToByteVector(key.GetPubKey()) << OP_CHECKSIG;
                    }
                    else {
                        std::unique_ptr<SigningProvider> provider = wallet.GetSolvingProvider(scriptPubKeyKernel);
                        if (!provider) {
                            LogPrint(BCLog::COINSTAKE, "CreateCoinStake : failed to get signing provider for output %s\n", pcoin.first->tx->vout[pcoin.second].ToString());
                            break;
                        }
                        CKeyID ckey = CKeyID(uint160(vSolutions[0]));
                        CPubKey pkey;
                        if (!provider.get()->GetPubKey(ckey, pkey)) {
                            LogPrint(BCLog::COINSTAKE, "CreateCoinStake : failed to get key for output %s\n", pcoin.first->tx->vout[pcoin.second].ToString());
                            break;
                        }
                        scriptPubKeyOut << ToByteVector(pkey) << OP_CHECKSIG;
                    }
                }
                else if (whichType == TxoutType::PUBKEY)
                    scriptPubKeyOut = scriptPubKeyKernel;
                else if (whichType == TxoutType::WITNESS_V0_KEYHASH || whichType == TxoutType::WITNESS_V1_TAPROOT) // pay to witness keyhash
                {
                    std::vector<valtype> vSolutionsTmp;
                    CScript scriptPubKeyTmp = GetScriptForDestination(destination);
                    Solver(scriptPubKeyTmp, vSolutionsTmp);
                    std::unique_ptr<SigningProvider> provider = wallet.GetSolvingProvider(scriptPubKeyTmp);
                    if (!provider) {
                        LogPrint(BCLog::COINSTAKE, "CreateCoinStake : failed to get signing provider for output %s\n", pcoin.first->tx->vout[pcoin.second].ToString());
                        break;
                    }
                    CKeyID ckey = CKeyID(uint160(vSolutionsTmp[0]));
                    CPubKey pkey;
                    if (!provider.get()->GetPubKey(ckey, pkey)) {
                        LogPrint(BCLog::COINSTAKE, "CreateCoinStake : failed to get key for output %s\n", pcoin.first->tx->vout[pcoin.second].ToString());
                        break;
                    }
                    scriptPubKeyOut << ToByteVector(pkey) << OP_CHECKSIG;
                    bMinterKey = true;
                }

                txNew.nTime -= n;
                txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
                nCredit += pcoin.first->tx->vout[pcoin.second].nValue;
                vwtxPrev.push_back(tx);

                if (bMinterKey) {
                    // extra output for minter key
                    txNew.vout.push_back(CTxOut(0, scriptPubKeyOut));
                    // redefine scriptPubKeyOut to send output to input address
                    scriptPubKeyOut = scriptPubKeyKernel;
                }
    
                txNew.vout.push_back(CTxOut(0, scriptPubKeyOut));
                LogPrint(BCLog::COINSTAKE, "CreateCoinStake : added kernel type=%d\n", (int)whichType);
                fKernelFound = true;
                break;
            }
        }
        if (fKernelFound)
            break; // if kernel is found stop searching
    }
    if (!fKernelFound)
        return false;
    if (nCredit == 0 || nCredit > nAllowedBalance)
        return false;

    for (const std::pair<const CWalletTx*, unsigned int> &pcoin : setCoins)
    {
        uint256 blockHash;
        CTransactionRef tx;
        if (!g_txindex->FindTx(pcoin.first->GetHash(), blockHash, tx)) {
            LogPrintf("couldnt retrieve tx %s\n", *pcoin.first->GetHash().ToString().c_str());
            continue;
        }

        // Attempt to add more inputs
        // Only add coins of the same key/address as kernel
        if (txNew.vout.size() == 2 && ((pcoin.first->tx->vout[pcoin.second].scriptPubKey == scriptPubKeyKernel || pcoin.first->tx->vout[pcoin.second].scriptPubKey == txNew.vout[1].scriptPubKey))
            && pcoin.first->GetHash() != txNew.vin[0].prevout.hash)
        {
            // Stop adding more inputs if already too many inputs
            if (txNew.vin.size() >= 10)
                break;
            // Stop adding more inputs if value is already pretty significant
            if (nCredit >= GetStakeCombineThreshold())
                break;
            // Stop adding inputs if reached reserve limit
            if (nCredit + pcoin.first->tx->vout[pcoin.second].nValue > nBalance - wallet.m_reserve_balance)
                break;
            // Do not add additional significant input
            if (pcoin.first->tx->vout[pcoin.second].nValue >= GetStakeCombineThreshold())
                continue;

            txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
            nCredit += pcoin.first->tx->vout[pcoin.second].nValue;
            vwtxPrev.push_back(tx);
        }
    }

    // Calculate reward
    CAmount nReward = nFees + GetProofOfStakeSubsidy();
    if (nReward < 0)
        return false;

    bool isDevFundEnabled = (wallet.m_donation_percentage > 0 && !Params().GetDevFundAddress().empty()) ? true : false;
    CAmount nDevCredit = 0;
    CAmount nMinerCredit = 0;

    if (isDevFundEnabled)
    {
        nDevCredit = (GetProofOfStakeSubsidy() * wallet.m_donation_percentage) / 100;
        nMinerCredit = nReward - nDevCredit;
        nCredit += nMinerCredit;
    }
    else
    {
        nCredit += nReward;
    }

    // Split stake
    if (nCredit >= GetStakeSplitThreshold())
        txNew.vout.push_back(CTxOut(0, scriptPubKeyOut));

    if (isDevFundEnabled)
        txNew.vout.push_back(CTxOut(0, Params().GetDevRewardScript()));

    // Set output amount
    if (txNew.vout.size() == (isDevFundEnabled ? 4u : 3u) + bMinterKey) {
        txNew.vout[1 + bMinterKey].nValue = (nCredit / 2 / CENT) * CENT;
        txNew.vout[2 + bMinterKey].nValue = nCredit - txNew.vout[1 + bMinterKey].nValue;
        if (isDevFundEnabled)
            txNew.vout[3 + bMinterKey].nValue = nDevCredit;
    }
    else
    {
        txNew.vout[1 + bMinterKey].nValue = nCredit;
        if (isDevFundEnabled)
            txNew.vout[2 + bMinterKey].nValue = nDevCredit;
    }

    // Sign
    int nIn = 0;

    if (wallet.IsLegacy()) {
        for (const auto &pcoin : vwtxPrev) {
            SignatureData empty;
            if (!SignSignature(*wallet.GetLegacyScriptPubKeyMan(), *pcoin, txNew, nIn++, SIGHASH_ALL, empty))
                return error("CreateCoinStake : failed to sign coinstake");
        }
    }
    else
    {
        // Fetch previous transactions (inputs):
        std::map<COutPoint, Coin> coins;
        for (const CTxIn& txin : txNew.vin) {
            coins[txin.prevout]; // Create empty map entry keyed by prevout.
        }
        wallet.chain().findCoins(coins);
        // Script verification errors
        std::map<int, bilingual_str> input_errors;
        int nTime = txNew.nTime;
        wallet.SignTransaction(txNew, coins, SIGHASH_ALL, input_errors);
        txNew.nTime = nTime;
    }

    // Limit size
    unsigned int nBytes = ::GetSerializeSize(TX_WITH_WITNESS(txNew));
    if (nBytes >= 1000000/5)
        return error("CreateCoinStake : exceeded coinstake size limit");

    // Successfully generated coinstake
    return true;
}
} // namespace wallet
