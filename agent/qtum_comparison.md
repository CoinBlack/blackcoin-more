# Qtum vs Blackcoin More — Staking Architecture Comparison

Cross-checked against both codebases. Qtum source: `../qtum/src/`. Blackcoin More source: `./src/`.

---

## 1. Kernel Hash Formula

**Identical** — both use the same SHA256 formula:

```
hash = SHA256(SHA256(
    nStakeModifier ||
    blockFromTime ||
    prevout.hash ||
    prevout.n ||
    nTimeTx
))
```

Requirement: `hash < bnTarget * nWeight`

| Detail | Blackcoin (`pos.cpp:100-102`) | Qtum (`pos.cpp:93-95`) |
|--------|-------------------------------|------------------------|
| Hash writer | `CHashWriter ss{}` | `HashWriter ss` |
| Inputs | `nStakeModifier << blockFromTime << prevout.hash << prevout.n << nTimeTx` | Identical |
| Target comparison | `hash > bnTarget * bnWeight` | Pre-fork: same. Post-fork: `hash / bnWeight > bnTarget` |
| Overflow fix | None | `nReduceBlocktimeHeight` fork divides hash by weight instead of multiplying target |

### blockFromTime derivation

| Code | Blackcoin | Qtum |
|------|-----------|------|
| `CheckKernel` (staking) | `coinPrev.nTime ? coinPrev.nTime : blockFrom->nTime` | `blockFrom->nTime` (always) |
| `CheckProofOfStake` (validation) | `coinPrev.nTime ? coinPrev.nTime : blockFrom->nTime` | `blockHeaderFrom->nTime` (always) |

Blackcoin stores `nTime` in `Coin` (UTXO) and falls back to block time when it's zero (v2 txs). Qtum removed `nTime` from `Coin` entirely — always uses the block header timestamp.

---

## 2. Coin.nTime

| | Blackcoin | Qtum |
|---|---|---|
| Field in `Coin` | `unsigned int nTime` (`coins.h:50`) | **Absent** |
| Serialized in UTXO | `VARINT(nTime)` between height and output (`coins.h:80`) | Only height + output |
| Constructor parameter | `Coin(..., int nTimeIn)` | `Coin(...)` — no nTime param |
| Used for? | `blockFromTime` fallback; `nTimeTx` ordering check (`tx_verify.cpp:191`) | Not needed |

Qtum's removal eliminates 4 bytes + VARINT overhead per UTXO and simplifies the `blockFromTime` logic.

---

## 3. `CBlockHeader` — prevoutStake

| | Blackcoin (`block.h:21-60`) | Qtum (`block.h:21-42`) |
|---|---|---|
| `prevoutStake` field | **No** | **Yes** — `COutPoint prevoutStake` (line 34) |
| Serialized in header hash | No (irrelevant) | **Yes** — `READWRITE(..., prevoutStake, ...)` (line 42) |
| Purpose | — | Commits to kernel input in block hash; enables delegation (header prevout != txin prevout) |
| Header signature includes | `nFlags` (excluded from hash) | `vchBlockSigDlgt` (in hash) |

Blackcoin finds the kernel input solely from `vtx[1]->vin[0].prevout`. Qtum also requires `block.prevoutStake == vtx[1]->vin[0].prevout` (unless delegated).

---

## 4. Transaction nTime Serialization

**Identical** — both use the same scheme derived from Blackcoin:

```cpp
// transaction.h — both codebases
if (tx.version < 2)
    s << tx.nTime;   // v1: serialized
else
    tx.nTime = 0;     // v2: stripped, defaults to 0 on deserialize
```

Both have `CURRENT_VERSION = 2` and `CMutableTransaction()` sets `nTime = GetAdjustedTimeSeconds()` in memory only.

Blackcoin descriptor wallet has a workaround: saves `nTime` before signing, restores after (`staking.cpp:497-499`). Qtum doesn't need this — `nTime` is irrelevant for v2 txs.

---

## 5. setStakeSeen (Duplicate Stake Prevention)

| | Blackcoin | Qtum |
|---|---|---|
| `setStakeSeen` | **Not present** | `std::set<std::pair<COutPoint, unsigned int>>` (`validation.cpp:136`) |
| Check | — | `setStakeSeen.count({header.prevoutStake, header.nTime})` before accepting header (`validation.cpp:6305`) |
| Purpose | — | Rejects duplicate `(prevout, timestamp)` across orphaned blocks |

Without this, v2 coinstakes with identical tx hashes across orphan windows could collide. Blackcoin relies on standard txid-level dedup, which is insufficient when the tx hash is identical.

---

## 6. CheckCoinStakeTimestamp

| | Blackcoin (`pos.cpp:43-50`) | Qtum (`pos.h:inline 33-36`) |
|---|---|---|
| Signature | `CheckCoinStakeTimestamp(nTimeBlock, nTimeTx)` | `CheckCoinStakeTimestamp(nTimeBlock, nHeight, consensusParams)` |
| Check (V2+) | `(nTimeBlock == nTimeTx) && ((nTimeTx & mask) == 0)` | `(nTimeBlock & StakeTimestampMask(nHeight)) == 0` only |
| nTimeTx param | Required (compares block vs tx time) | Not needed (no tx nTime) |

Qtum's version is simpler because there's no `tx.nTime` to compare against.

---

## 7. Staking Loop

### Blackcoin (`miner.cpp:735-906`)

```
PoSMiner loop:
  wait for sync / wallet unlocked
  CreateNewBlock:
    → set txCoinStake.nTime = now & ~0xf
    → safety bump if ≤ MTP
    → timer guard (m_last_coin_stake_search_time)
    → CreateCoinStake(nSearchInterval=1)
      → for each UTXO:
          for n=0; n<1; n++:
            CheckKernel(txNew.nTime - n)
              → cache hit? check + double-check
              → cache miss? GetCoin + GetAncestor + check
    → on success: set pblock->nTime, update timer
  if failed: sleep(pos_timio = 500 + 30*sqrt(UTXOs)) ms
  if success: sleep(16000 + rand(4000)) ms
  wake-on-block via cv_new_block
```

### Qtum (`miner.cpp:1579-1628`)

```
StakeMiner::Run loop:
  CacheData():
    → SelectCoinsForStaking
    → CreateEmptyBlock (template)
    → UpdateMinerStakeCache — populate minerStakeCache for ALL UTXOs
    → beginningTime = now & ~mask
    → endingTime = beginningTime + MAX_STAKE_LOOKAHEAD (48s = 3 windows)
  
  for blockTime = beginningTime; blockTime < endingTime; blockTime += 16:
    CanCreateBlock(blockTime):
      → SloveBlock(blockTime):
          for each UTXO:
            CheckKernelCache(blockTime, prevout, minerStakeCache)
              → cache hit? check hash (NO double-check, NO fallback)
              → cache miss? return false
          store results in mapSolveBlockTime / mapSolvedBlock
      → if any solved:
          CreateNewBlock(blockTime):
            SignBlock(tryOnly) → get staker address
            BlockAssembler::CreateNewBlock (full with txs)
          SignNewBlock(blockTime):
            SignBlock (real)
            wait until blockTime arrives (spin-wait 100ms or 3s)
            CheckStake → submit
  
  Sleep(nMinerSleep) = 5000ms (20000ms on min difficulty)
```

### Key Differences

| | Blackcoin | Qtum |
|---|---|---|
| Windows per batch | **1** (current window only) | **3** (forward scan 48s lookahead) |
| Cache reuse | Per-window (rebuilds each CreateCoinStake) | Once per batch, reused across 3 windows |
| Kernel check function | `CheckKernel` with cache (double-checks) | `CheckKernelCache` (no double-check, no fallback) |
| Priority | Normal | Boosted to `ABOVE_NORMAL` during CreateNewBlock |
| Spin-wait on future time | No (only current window) | Yes — waits for target blockTime to arrive |

---

## 8. Stake Cache

### Data structure

Both use identical `CStakeCache`:
```cpp
struct CStakeCache {
    uint32_t blockFromTime;
    CAmount amount;
};
```

Stored in `std::map<COutPoint, CStakeCache>`.

### CacheKernel — Population

Both skip already-cached entries, fetch `Coin` via `GetCoin`, check maturity, find ancestor block, then insert `(blockFromTime, amount)`.

| | Blackcoin (`pos.cpp:218-241`) | Qtum (`pos.cpp:484-507`) |
|---|---|---|
| blockFromTime stored | `coinPrev.nTime ? coinPrev.nTime : blockFrom->nTime` | `blockFrom->nTime` |
| Maturity param | `nCoinbaseMaturity` (fixed) | `CoinbaseMaturity(nHeight)` (variable post-fork) |
| Coin lookup | `view.GetCoin(prevout, coinPrev)` | `ViewGetCoin(view, prevout, coinPrev)` |

### Cache hit: CheckKernel with cache

| | Blackcoin (`pos.cpp:199-214`) | Qtum (`pos.cpp:460-469`) |
|---|---|---|
| Check hash | `CheckStakeKernelHash(..., stake.blockFromTime, stake.amount, ...)` | Same |
| Double-check | **Yes** — calls no-cache `CheckKernel(... view)` | **Same** |
| Reorg safety | Protected | Protected (in this path) |

### Cache-only: CheckKernelCache — Qtum only (`pos.cpp:472-482`)

```cpp
bool CheckKernelCache(..., const map<COutPoint, CStakeCache>& cache, ...) {
    auto it = cache.find(prevout);
    if (it != cache.end())
        return CheckStakeKernelHash(..., stake.blockFromTime, stake.amount, ...);
    return false;  // NO fallback, NO double-check
}
```

Used exclusively by the forward scan (`miner.cpp:1865`). Trading safety for speed — no reorg guard, no disk I/O on miss.

### Cache invalidation

| | Blackcoin | Qtum |
|---|---|---|
| Clear trigger | `cache.size() > setCoins.size() + 100` | Same (all caches) |
| When cleared | Per `CreateCoinStake` call | Per `UpdateMinerStakeCache` call |
| Cache types | `stakeCache` (wallet) | `stakeCache`, `stakeDelegateCache`, `minerStakeCache`, `addressStakeCache` |
| Miner flag | None | `fHasMinerStakeCache` — wallet uses `minerStakeCache` if set |
| `-stakecache` flag | Checked in both wallet and miner path | Checked only in wallet path; miner **always caches** |

---

## 9. Maturity

| | Blackcoin | Qtum |
|---|---|---|
| Param | `nCoinbaseMaturity` (fixed int) | `nCoinbaseMaturity` + `nRBTCoinbaseMaturity` |
| Function | Direct field access | `CoinbaseMaturity(nHeight)` — switches at `nReduceBlocktimeHeight` |
| Mainnet | 500 | 500 |
| Testnet | 10 | 10 |

---

## 10. Timestamp Mask

| | Blackcoin | Qtum |
|---|---|---|
| Fixed | `nStakeTimestampMask = 0xf` (16s) | `nStakeTimestampMask = 0xf` initially |
| Variable | No | `StakeTimestampMask(nHeight)` — can switch to `nRBTStakeTimestampMask` post-`nReduceBlocktimeHeight` |
| Downscale factor | N/A | `TimestampDownscaleFactor(nHeight)` — adjusts lookahead, sleep, and buffer times proportionally |

---

## 11. StakeModifier

**Identical** in both codebases:

```cpp
uint256 ComputeStakeModifier(const CBlockIndex* pindexPrev, const uint256& kernel) {
    CHashWriter ss;
    ss << kernel << pindexPrev->nStakeModifier;
    return ss.GetHash();
}
```

Called at `validation.cpp:2708` (both):
- PoS block: `kernel = block.vtx[1]->vin[0].prevout.hash`
- PoW block: `kernel = block.GetHash()`

---

## 12. Delegation

| | Blackcoin | Qtum |
|---|---|---|
| Offline staking | **Not supported** | **Proof-of-Delegation** via EVM smart contract (`stake.cpp:284-994`, `pos.cpp:176-285`) |
| Header prevout vs txin prevout | Always identical | Can differ when delegated |
| Block signature | Standard coinstake key | Can include delegation proof (`vchBlockSigDlgt`) |
| Super staker | N/A | Minimum UTXO value check, fee splitting |

---

## Summary: Evolution of Blackcoin PoS → Qtum

PPCoin → Blackcoin v1/v2 (nTime in tx, 16s mask, nSearchInterval=1 timer guard) → Blackcoin v3/v3.1 (v2 drops nTime from serialization, coin.nTime fallback, safety bump, stake cache) → Qtum (removed nTime from Coin, forward scan 3 windows, CheckKernelCache no double-check, prevoutStake in header, setStakeSeen, multi-cache, delegated staking, adjustable parameters via fork).

---

## Compared Files

| Component | Blackmore | Qtum |
|---|---|---|
| Kernel hash | `pos.cpp` | `pos.cpp` |
| Coin UTXO | `coins.h` (has nTime) | `coins.h` (no nTime) |
| Block header | `block.h` (no prevoutStake) | `block.h` (has prevoutStake) |
| Staking loop | `miner.cpp` (current window + poll) | `miner.cpp` (forward 3 windows) |
| Stake cache | `wallet/wallet.h` | `wallet/wallet.h` (4 types) |
| Anti-dupe | None | `validation.cpp` (`setStakeSeen`) |
| Delegation | None | `wallet/stake.cpp` (EVM-based) |
| Parameters | Fixed in `consensus/params.h` | Adjustable via forks
