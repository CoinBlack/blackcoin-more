// Copyright (c) 2014-2023 The Blackcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Staking start/stop algos by Qtum
// Copyright (c) 2016-2023 The Qtum developers

#include <common/args.h>
#include <wallet/coincontrol.h>
#include <wallet/receive.h>
#include <wallet/staking.h>
#include <node/miner.h>

namespace wallet {

static int64_t GetStakeCombineThreshold() { return 250 * COIN; }
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
        wallet.cv_new_block.notify_one();
        StakeCoins(wallet, false);
        wallet.threadStakeMinerGroup = 0;
        wallet.m_stop_staking_thread = false;
    }
}

uint64_t GetStakeWeight(const CWallet& wallet)
{
    LOCK(wallet.cs_wallet);
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
    // Staking always allows used addresses, because staking inherently spends from and
    // sends rewards back to the same address. The avoid_reuse privacy feature from Bitcoin
    // is not applicable to staking.
    bool allow_used_addresses = true;
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
            const COutPoint outpoint(Txid::FromUint256(wtxid), i);

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

            bool spendable = ((mine & ISMINE_SPENDABLE) != ISMINE_NO);
            if (!spendable && ((mine & ISMINE_WATCH_ONLY) != ISMINE_NO) && coinControl && coinControl->fAllowWatchOnly) {
                bool solvable = provider ? InferDescriptor(output.scriptPubKey, *provider)->IsSolvable() : false;
                spendable = solvable;
            }

            // Filter by spendable outputs only
            if (!spendable && params.only_spendable) continue;

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
    AssertLockHeld(wallet.cs_wallet);
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

    LOCK2(cs_main, wallet.cs_wallet);
    txNew.vin.clear();
    txNew.vout.clear();

    // Mark coin stake transaction
    txNew.vout.push_back(CTxOut(0, CScript()));

    // Choose coins to use
    const auto bal = GetBalance(wallet);
    CAmount nBalance = bal.m_mine_trusted;
    if (fAllowWatchOnly)
        nBalance += bal.m_watchonly_trusted;

    if (nBalance <= wallet.m_reserve_balance)
        return false;

    std::set<std::pair<const CWalletTx*, unsigned int> > setCoins;
    std::vector<CTransactionRef> vwtxPrev;
    CAmount nAllowedBalance = nBalance - wallet.m_reserve_balance;

    // Select coins with suitable depth
    CAmount nValueInUnused = 0;
    if (!SelectCoinsForStaking(wallet, nAllowedBalance, setCoins, nValueInUnused))
        return false;

    if (setCoins.empty())
        return false;

    if (wallet.stakeCache.size() > setCoins.size() + 100) {
        wallet.stakeCache.clear();
    }
    if (gArgs.GetBoolArg("-stakecache", node::DEFAULT_STAKE_CACHE)) {
        for (const auto& pcoin : setCoins) {
            COutPoint prevoutStake = COutPoint(pcoin.first->GetHash(), pcoin.second);
            CacheKernel(wallet.stakeCache, prevoutStake, pindexPrev, wallet.chain().getCoinsTip());
        }
    } // blackcoin: stakecache

    CAmount nCredit = 0;
    bool fKernelFound = false;
    CScript scriptPubKeyKernel, scriptPubKeyOut, scriptCarrier;
    CPubKey minterPubKey;
    TxoutType whichType = TxoutType::NONSTANDARD;

    if (!wallet.stakeCache.empty()) {
        LogPrint(BCLog::COINSTAKE, "[%s] CheckKernel: cache active, size=%zu\n", wallet.GetName(), wallet.stakeCache.size());
    } // blackcoin: stakecache

    for (const std::pair<const CWalletTx*, unsigned int> &pcoin : setCoins)
    {
        // blackcoin: Optimization
        // We use the cached transaction data in CWalletTx instead of hitting disk with g_txindex->FindTx
        // This reduces block creation time from ~100s to <100ms
        static int nMaxStakeSearchInterval = 60;
        for (unsigned int n=0; n<std::min(nSearchInterval,(int64_t)nMaxStakeSearchInterval) && !fKernelFound; n++)
        {
            // Search backward in time from the given txNew timestamp
            // Search nSearchInterval seconds back up to nMaxStakeSearchInterval
            COutPoint prevoutStake = COutPoint(pcoin.first->GetHash(), pcoin.second);
            if (CheckKernel(pindexPrev, nBits, txNew.nTime - n, prevoutStake,
                            wallet.chain().getCoinsTip(), wallet.stakeCache))
            {
                // Found a kernel
                LogPrint(BCLog::COINSTAKE, "[%s] CreateCoinStake: kernel found\n", wallet.GetName());
                std::vector<valtype> vSolutions;
                scriptPubKeyKernel = pcoin.first->tx->vout[pcoin.second].scriptPubKey;
                whichType = Solver(scriptPubKeyKernel, vSolutions);

                if (whichType != TxoutType::PUBKEY && whichType != TxoutType::PUBKEYHASH && whichType != TxoutType::WITNESS_V0_KEYHASH && whichType != TxoutType::WITNESS_V1_TAPROOT)
                {
                    LogPrint(BCLog::COINSTAKE, "[%s] CreateCoinStake: no support for kernel type=%s\n", wallet.GetName(), GetTxnOutputType(whichType));
                    break;  // only support pay to public key and pay to address and pay to witness keyhash
                }

                // Look up the SignKey pubkey for the OP_RETURN carrier
                {
                    CScript scriptPubKeyTmp = GetScriptForDestination(destination);
                    std::vector<valtype> vSolutionsTmp;
                    TxoutType whichTypeTmp = Solver(scriptPubKeyTmp, vSolutionsTmp);
                    if (whichTypeTmp != TxoutType::PUBKEYHASH || vSolutionsTmp.empty()) {
                        LogPrint(BCLog::COINSTAKE, "[%s] CreateCoinStake: SignKey destination is not P2PKH (type=%s)\n", wallet.GetName(), GetTxnOutputType(whichTypeTmp));
                        break;
                    }
                    std::unique_ptr<SigningProvider> provider = wallet.GetSolvingProvider(scriptPubKeyTmp);
                    if (!provider) {
                        LogPrint(BCLog::COINSTAKE, "[%s] CreateCoinStake: failed to get signing provider for minter key\n", wallet.GetName());
                        break;
                    }
                    CKeyID ckey = CKeyID(uint160(vSolutionsTmp[0]));
                    if (!provider.get()->GetPubKey(ckey, minterPubKey)) {
                        LogPrint(BCLog::COINSTAKE, "[%s] CreateCoinStake: failed to get minter pubkey\n", wallet.GetName());
                        break;
                    }
                }
                scriptCarrier = CScript() << OP_RETURN << ToByteVector(minterPubKey);

                // Automatically upgrade legacy P2PK kernels to P2PKH rewards
                if (whichType == TxoutType::PUBKEY) {
                    scriptPubKeyOut = GetScriptForDestination(PKHash(CPubKey(vSolutions[0])));
                } else {
                    scriptPubKeyOut = scriptPubKeyKernel;
                }

                txNew.nTime -= n;
                txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
                nCredit += pcoin.first->tx->vout[pcoin.second].nValue;
                vwtxPrev.push_back(pcoin.first->tx);

                // Append timestamp to OP_RETURN carrier
                uint32_t nTime = txNew.nTime;
                unsigned char timeBytes[4] = {
                    static_cast<unsigned char>(nTime & 0xff),
                    static_cast<unsigned char>((nTime >> 8) & 0xff),
                    static_cast<unsigned char>((nTime >> 16) & 0xff),
                    static_cast<unsigned char>((nTime >> 24) & 0xff)
                };
                scriptCarrier << std::vector<unsigned char>(timeBytes, timeBytes + 4);

                // OP_RETURN carrier in vout[1], reward in vout[2]
                txNew.vout.push_back(CTxOut(0, scriptCarrier));
                txNew.vout.push_back(CTxOut(0, scriptPubKeyOut));
                LogPrint(BCLog::COINSTAKE, "[%s] CreateCoinStake: added kernel type=%d\n", wallet.GetName(), (int)whichType);
                fKernelFound = true;

                if (txNew.nTime < pindexPrev->GetMedianTimePast() + 1) {
                    LogPrint(BCLog::COINSTAKE, "[%s] CreateCoinStake: kernel timestamp %d <= MTP %d, aborting\n",
                             wallet.GetName(), txNew.nTime, pindexPrev->GetMedianTimePast());
                    return false;
                }
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

    // Build the legacy P2PK script from the carrier pubkey so old P2PK reward
    // UTXOs can still be swept into coinstakes via input combining. Old P2PK
    // rewards minted before the OP_RETURN carrier change have a different
    // scriptPubKey than the current kernel (which may be P2PKH/P2WPKH/P2TR),
    // so matching scriptPubKeyKernel alone would miss them.
    // P2PK sweep applies only to PUBKEYHASH kernels (the type that historically
    // produced P2PK rewards).
    CScript scriptPubKeyP2PK = CScript() << ToByteVector(minterPubKey) << OP_CHECKSIG;
    bool combineP2PK = (whichType == TxoutType::PUBKEYHASH);

    for (const std::pair<const CWalletTx*, unsigned int> &pcoin : setCoins)
    {
        // Attempt to add more inputs. Only add coins whose script matches the
        // current kernel script (same-type combining), or for P2PKH kernels
        // additionally match legacy P2PK rewards derived from the same SignKey.
        if (txNew.vout.size() == 3 && (pcoin.first->tx->vout[pcoin.second].scriptPubKey == scriptPubKeyKernel
            || (combineP2PK && pcoin.first->tx->vout[pcoin.second].scriptPubKey == scriptPubKeyP2PK))
            && COutPoint(pcoin.first->GetHash(), pcoin.second) != txNew.vin[0].prevout)
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
            // Do not add input with timestamp later than coinstake (bad-txns-time-earlier-than-input)
            // Use nTimeSmart (block time for confirmed txs) not tx->nTime, which is 0 for v2
            // transactions since nTime is not serialized for version >= 2 (see transaction.h:233-236).
            // The consensus check in tx_verify.cpp:191 uses coin.nTime, which for v2 txs equals
            // the block header time (coins.cpp:129), matching nTimeSmart for confirmed coinstakes.
            if (pcoin.first->nTimeSmart > txNew.nTime)
                continue;

            txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
            nCredit += pcoin.first->tx->vout[pcoin.second].nValue;
            vwtxPrev.push_back(pcoin.first->tx);
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
    // Reward is always at vout[2] (vout[0]=marker, vout[1]=OP_RETURN carrier)
    if (txNew.vout.size() == (isDevFundEnabled ? 5u : 4u)) {
        txNew.vout[2].nValue = (nCredit / 2 / CENT) * CENT;
        txNew.vout[3].nValue = nCredit - txNew.vout[2].nValue;
        if (isDevFundEnabled)
            txNew.vout[4].nValue = nDevCredit;
    }
    else
    {
        txNew.vout[2].nValue = nCredit;
        if (isDevFundEnabled)
            txNew.vout[3].nValue = nDevCredit;
    }

    // Sign
    int nIn = 0;

    if (wallet.IsLegacy()) {
        for (const auto &pcoin : vwtxPrev) {
            SignatureData empty;
            if (!SignSignature(*wallet.GetLegacyScriptPubKeyMan(), *pcoin, txNew, nIn++, SIGHASH_ALL, empty)) {
                LogError("%s: failed to sign coinstake\n", __func__);
                return false;
            }
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
        if (!wallet.SignTransaction(txNew, coins, SIGHASH_ALL, input_errors)) {
            for (const auto& [idx, err] : input_errors) {
                LogError("%s: failed to sign coinstake input %d: %s\n", __func__, idx, err.original);
            }
            return false;
        }
        txNew.nTime = nTime;
    }

    // Limit size
    unsigned int nBytes = ::GetSerializeSize(TX_WITH_WITNESS(txNew));
    if (nBytes >= 1000000/5) {
        LogError("%s: exceeded coinstake size limit\n", __func__);
        return false;
    }

    // Successfully generated coinstake
    return true;
}
} // namespace wallet
