# Staking Probabilities and UTXO Analysis

For Blackcoin More PoS staking, the expected reward rate depends on **total wallet weight relative to network weight**. UTXO distribution only affects variance, cooldown dynamics, and CPU overhead — not long-term expected rewards.

---

## 1. Kernel Hash Probability

The PoS kernel check (`pos.cpp:CheckStakeKernelHash`):
```
SHA256(nStakeModifier + txPrev.nTime + prevout.hash + prevout.n + nTimeTx) < bnTarget * amount
```

For a single UTXO of `amount` in one 16-second window:
```
P(success per window) ≈ amount * target / 2^256
```

Where `target` is derived from current `nBits`, `amount` is in satoshis, and each UTXO gets one hash attempt per 16-second window.

### 1.1 Minimum Input Size (current mainnet conditions)

From recent mainnet (`nBits` ≈ `1a0efc08`, PoS difficulty ≈ 1,120,000):

| Chance per 16s window | Required BLK |
|---|---|
| 0.001% | ~480 BLK |
| 0.01% | ~4,800 BLK |
| 0.1% | ~48,000 BLK |
| 1% | ~480,000 BLK |
| 50% | ~24,000,000 BLK |

UTXOs below ~100-200 BLK have near-zero practical chance at current difficulty.

---

## 2. Total Wallet Weight vs Per-UTXO Chances

Total probability in one window is the sum of per-UTXO probabilities:
```
P(total) ≈ Σ (amount_i * target / 2^256) = total_weight * target / 2^256
```

**Total weight** determines expected reward rate. UTXO distribution only affects:
- **Variance** (fewer large UTXOs = higher variance)
- **Cooldown impact** (UTXO locked ~500 blocks after staking)
- **CPU load** (more UTXOs = more hash attempts per window)
- **Coinstake tx size** (split behavior)

---

## 3. Expected Time to Find a Block

`getstakinginfo` example:
```json
{
  "difficulty": 801060.5454231506,
  "weight": 79557355306641,
  "netstakeweight": 1086262969416965,
  "expectedtime": 913,
  "search-interval": 16
}
```

### 3.1 Formula

Implemented in `src/wallet/rpc/staking.cpp:76`:
```cpp
uint64_t nExpectedTime = staking ? 1.0455 * nTargetSpacing * nNetworkWeight / nWeight : 0;
```

Where:
- `nTargetSpacing = 64` seconds (`src/kernel/chainparams.cpp:114`)
- `1.0455` = hardcoded orphan-rate correction factor (~4.55%)
- `nNetworkWeight / nWeight` = inverse of your network share

For the example: `1.0455 * 64 * (10862629/795574) ≈ 913 seconds` — matches RPC output.

### 3.2 Actual Network Orphan Rate

From `chainz.cryptoid.info/blk/orphans.dws`: ~40 stale/orphan blocks/day, ~1,280 successful/day.
- **Actual orphan rate**: 40 / 1,320 ≈ **3.0%**
- **Effective block time**: 86,400 / 1,280 ≈ **67.5 seconds**

The hardcoded 1.0455 (4.55%) overestimates orphans because it also lumps in network propagation latency, clock drift, and stake modifier timing variance.

### 3.3 Qt GUI vs RPC

Both use the same formula (`src/qt/bitcoingui.cpp:1599` vs `src/wallet/rpc/staking.cpp:76`): `1.0455 * nTargetSpacing * nNetworkWeight / nWeight`. RPC returns `uint64_t seconds`; GUI formats as human-readable string. Estimates are identical.

---

## 4. UTXO Selection Behavior

### 4.1 Why some UTXOs are reused and others never stake

The wallet selects eligible UTXOs in `SelectCoinsForStaking` and `AvailableCoinsForStaking`:
- UTXOs are returned in wallet map order (`mapWallet`, keyed by txid), not sorted by value
- `CreateCoinStake` iterates them in that order
- Each UTXO's kernel hash is tested against the current 16-second window

Combined with 16-second timer guard and 500-block cooldown:
- **Large UTXOs** have higher per-attempt chance to clear `bnTarget * amount`, so they win more often
- **Small UTXOs** rarely clear the threshold
- **Repeated UTXOs** in logs are typically those that passed the kernel check in consecutive windows

### 4.2 Coin selection is local

Other wallets only affect difficulty and stake modifier — they don't influence which of your UTXOs is tested.

---

## 5. StakeCombineThreshold and Split Behavior

`GetStakeCombineThreshold()` is hardcoded in `src/wallet/staking.cpp:16`:
```cpp
static int64_t GetStakeCombineThreshold() { return 250 * COIN; }
static int64_t GetStakeSplitThreshold() { return 2 * GetStakeCombineThreshold(); }
```

The **combine** logic at `src/wallet/staking.cpp:415` stops adding more inputs once `nCredit >= GetStakeCombineThreshold()`. The **split** logic at line 458 uses the split threshold: `if (nCredit >= GetStakeSplitThreshold()) { split reward into 2 outputs }`. So splitting happens at **500 BLK**.

Splitting preserves total weight — it doesn't change expected rewards.

### 5.1 Input Combining Guard (COutPoint fix, June 29)

The combining loop at `staking.cpp:412-414` matches UTXOs by script and excludes the kernel input:

```cpp
if (txNew.vout.size() == 3 && (pcoin.first->tx->vout[pcoin.second].scriptPubKey == scriptPubKeyKernel
    || (combineP2PK && pcoin.first->tx->vout[pcoin.second].scriptPubKey == scriptPubKeyP2PK))
    && COutPoint(pcoin.first->GetHash(), pcoin.second) != txNew.vin[0].prevout)
```

**Bug fixed June 29**: the guard was `pcoin.first->GetHash() != txNew.vin[0].prevout.hash`, comparing only the transaction hash. This rejected **all** other outputs from the kernel's parent transaction. Real-world impact: an `optimizeutxoset` transaction creating 42 × 50 BLK P2WPKH outputs to the same address would only have 1 combined with the kernel — all 41 siblings from the same tx were skipped because they shared the same txid. Only UTXOs from **different** transactions could be combined. Fixed to compare the full `COutPoint` (txid + vout index).

### 5.2 `optimizeutxoset` RPC

The `optimizeutxoset` RPC (`src/wallet/rpc/spend.cpp:351`) consolidates wallet UTXOs into uniform outputs:

```
optimizeutxoset <address> <amount> [transmit=false] [fromAddress]
```

- Selects all available coins (optionally filtered by `fromAddress`)
- Calculates fee based on estimated total vsize (inputs + N outputs)
- Creates as many outputs of `amount` BLK as possible: `while (remaining > amount + fee) { recipients.push_back(recipient); remaining -= amount; }`
- Change goes to the same address (`coin_control.destChange = dest`)
- Output type is determined by the address format: Bech32 → P2WPKH, Bech32m → P2TR, legacy → P2PKH

Example: `optimizeutxoset blk1qzam... 50` with 2159 BLK creates 42 × 50 BLK P2WPKH outputs + 1 × 9.89542 BLK change, all to the same address. These uniform outputs are ideal for staking kernel selection. After the COutPoint fix, all 42 can be combined into a single coinstake (up to the 250 BLK threshold and 10-input limit).

---

## 6. Network Timing Edge Cases

### 6.1 Clock drift advantage

`FutureDrift` (`validation.cpp:145`) accepts blocks up to **15 seconds in the future**:
```cpp
return Params().GetConsensus().IsProtocolV2(nTime) ? nTime + 15 : nTime + 10 * 60;
```
A staker with +14s clock drift can hash the next 16s boundary ~14s before honest nodes, gaining a head start.

### 6.2 Multi-node staking with same wallet

Two nodes staking the same wallet with different `stakecombinethreshold` produce different coinstake txids for the same kernel. Only one can win. To avoid self-competition: use identical settings, run one staker per wallet, or partition UTXOs.

---

## 7. Key Takeaways

1. **Total wallet weight** determines expected reward rate, not UTXO count.
2. **Minimum effective stake** at current difficulty is ~480 BLK for 0.001% chance per 16s window.
3. **Orphan rate** affects expected time between blocks but not per-attempt probability.
4. **Expected time formula** uses hardcoded ~4.55% orphan estimate; actual observed rate is ~3% + latency.
5. **Deterministic coin selection** walks UTXOs in wallet order; large UTXOs have higher per-window chance.
6. **Clock drift up to 15s** is valid and exploitable for staking advantage.
7. **Multi-node setups** with mismatched settings cause self-orphans.
8. **Staker timing**: uses single-responsibility design entirely inside `miner.cpp` via `MsUntilNextWindow()`, with the wallet acting purely as a wake trigger. See `agent/SafetyBump.md`.

---

## 8. Useful RPCs

```bash
blackmore-cli getstakinginfo
blackmore-cli getdifficulty
blackmore-cli gettxoutsetinfo muhash false
blackmore-cli getindexinfo
```
