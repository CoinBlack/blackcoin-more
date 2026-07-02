# Blackcoin More v26.2.0 Staking Walkthrough

Historical reference for the staking design in v26.2.0. v28-CORE changes are noted where applicable.

---

## 1. Entry Points (`wallet/staking.cpp`)

- `StartStake()`: validates wallet (has private keys, not blank, keypool populated), sets `m_enabled_staking=true`, calls `StakeCoins(wallet, true)`.
- `StakeCoins()`: joins existing staker thread, spawns new `ThreadStakeMiner`.
- `StopStake()`: sets `m_stop_staking_thread=true`, `m_enabled_staking=false`, joins thread.

---

## 2. ThreadStakeMiner → PoSMiner (`node/miner.cpp`)

`ThreadStakeMiner()` wraps `PoSMiner(pwallet)` in an infinite retry loop — catches exceptions, logs, restarts.

---

## 3. PoSMiner — Main Loop

**Phase A — Setup (one-time at thread start):**
Resolves staking destination — looks up "Staking Legacy Address" in address book; creates one via `GetNewDestination(LEGACY, ...)` if not found.

Computes `pos_timio`:
```cpp
pos_timio = GetArg("-staketimio", DEFAULT_STAKETIMIO) + 30 * sqrt(UTXOs);
// DEFAULT_STAKETIMIO = 500 (ms)
```

**Phase B — Infinite loop:**
```
while (true) {
    1. Wait while wallet locked / staking disabled / rescanning / importing → SleepStaker(pwallet, 5000)
    2. Wait until ≥1 peer connected AND IBD complete → SleepStaker(pwallet, 10000)
    3. Wait until sync ≥ 99.6% → SleepStaker(pwallet, 10000)
    4. Remember current tip
    5. CreateNewBlock(pwallet, &fPoSCancel, ...)
       → If null: fPoSCancel? sleep pos_timio : error, return
    6. If block found: SignBlock → ProcessBlockFound → sleep 16-20s
    7. Sleep pos_timio, continue
}
```

In v26.x, there is **no wake-on-block mechanism** — staker always sleeps the full duration.

---

## 4. SleepStaker (`node/miner.cpp:806-829`)

```cpp
bool SleepStaker(CWallet *pwallet, uint64_t milliseconds) {
    uint64_t seconds = milliseconds / 1000;
    milliseconds %= 1000;
    for (unsigned int i = 0; i < seconds; i++) {
        if(!pwallet->IsStakeClosing()) Sleep(1s);
        else return false;
    }
    if (milliseconds) {
        if(!pwallet->IsStakeClosing()) Sleep(milliseconds);
        else return false;
    }
    return !pwallet->IsStakeClosing();
}
```

Sleep is broken into 1-second chunks for shutdown responsiveness. No `cv_new_block`, no wake-on-block.

---

## 5. CreateNewBlock — Timer Guard (`node/miner.cpp:213-294`)

```cpp
static int64_t nLastCoinStakeSearchTime = GetAdjustedTimeSeconds();

if (pwallet) {
    *pfPoSCancel = true;
    pblock->nBits = GetNextTargetRequired(...);

    CMutableTransaction txCoinStake;
    txCoinStake.nTime &= ~nStakeTimestampMask;

    int64_t nSearchTime = txCoinStake.nTime;

    if (nSearchTime > nLastCoinStakeSearchTime) {
        if (CreateCoinStake(..., 1, txCoinStake, ...)) {
            if (txCoinStake.nTime >= pindexPrev->MTP+1) {
                *pfPoSCancel = false;
            }
        }
        nLastCoinStakeSearchTime = nSearchTime;
    }

    if (*pfPoSCancel) return nullptr;
}
```

Key behaviors:
1. **nSearchInterval=1**: only searches the boundary timestamp, no backward search.
2. **Timer guard** `nSearchTime > nLastCoinStakeSearchTime`: fires at most once per 16s window.
3. **Bug in 26.x**: `txCoinStake.nTime` is uninitialized garbage. `nSearchTime = garbage & ~0xf`. The guard has ~50% first-call pass rate. The kernel timestamp is correct because `CreateCoinStake` wipes it when finding a kernel.
4. **Static local = global**: shared across all wallets. Multi-wallet staking serializes.

---

## 6. CreateCoinStake — Kernel Search (`wallet/staking.cpp:252-476`)

1. **Select coins**: `SelectCoinsForStaking` → greedy pick, NOT knapsack.
   `AvailableCoinsForStaking` filters: depth ≥ max(1, coinbaseMaturity=500), depth ≤ 999999, value ≥ 0.1 BLK, spendable, not locked, not immature, not conflicted.

2. **Kernel search**: backward loop with `nSearchInterval=1` — only `n=0` is checked:
   ```cpp
   for (n=0; n < min(nSearchInterval, 60); n++) {
       if (CheckKernel(..., txNew.nTime - n, ...)) { /* found */ }
   }
   ```

3. **Add extra inputs**: up to 10 additional inputs matching same scriptPubKey, up to `GetStakeCombineThreshold`.

4. **Split/donate**: if total credit ≥ 1000 BLK, split into 2 outputs. Dev fund output if enabled.

5. **Sign**: Legacy → `SignSignature` per input. Descriptors → `wallet.SignTransaction`.

---

## 7. CheckStakeKernelHash (`pos.cpp:77-122`)

```
hash = SHA256(nStakeModifier + blockFromTime + prevout.hash + prevout.n + nTimeTx)
target = bnTarget * nValueIn * COIN
```
If `hash < target`, it's a kernel. Same `(nStakeModifier, blockFromTime, prevout, nTimeTx)` always produces the same hash. With `nSearchInterval=1`, each UTXO gets exactly one hash attempt per 16s window.

---

## 8. CheckProofOfStake (`pos.cpp:136-193`)

Checks:
1. First input is valid coin in UTXO set
2. Maturity ≥ 500 confirmations
3. Block ancestor found
4. `VerifySignature(coinPrev, ..., tx, 0, SCRIPT_VERIFY_NONE)` — signature check with NO flags
5. `CheckStakeKernelHash` with `nTimeTx = coinstake.nTime ?: block.nTime`

`SCRIPT_VERIFY_NONE` means P2PK/P2PKH kernels get proper CHECKSIG verification; P2WPKH/P2SH/P2TR kernels pass trivially (null witness, no P2SH redeem script).

---

## 9. CheckKernel + CStakeCache (`pos.cpp:207-265`)

Without cache: full lookup — `GetCoin()` → maturity → `GetAncestor()` → `CheckStakeKernelHash`.
With cache: uses cached `(blockFromTime, amount)`, then double-checks with uncached version.

`CacheKernel` pre-populates the cache before the search loop.

---

## 10. CoinStake Timestamp Rules (`pos.cpp:27-38`)

```cpp
bool CheckCoinStakeTimestamp(int64_t nTimeBlock, int64_t nTimeTx) {
    if (IsProtocolV2(nTimeBlock))
        return (nTimeBlock == nTimeTx) && ((nTimeTx & 0xf) == 0);
    else
        return (nTimeBlock == nTimeTx);
}
```

Protocol V2+ (active since 2014): block nTime must equal coinstake nTime, both with bottom 4 bits cleared (boundary alignment).

---

## 11. Block Timestamp Checks

- `block.nTime > FutureDrift(adjustedTime)` → reject "time-too-new"
- `CheckCoinStakeTimestamp(block.nTime, coinstake.nTime)` → reject if mismatch
- Transaction timestamp: if v2 (`nTime=0`), uses `GetAdjustedTimeSeconds()` as nTimeTx

---

## 12. ProcessBlockFound (`miner.cpp:786-803`)

Checks `CheckProofOfStake`, then submits via `chainman.ProcessNewBlock`. 
*(Note: In v26.x there was a stale check `hashPrevBlock != activeTip` here which caused the counterattack-window problem, where honest nodes self-rejected valid blocks if a competitor arrived first. This was removed in v28-CORE to allow the node to properly submit competing forks).*

---

## 13. Block Notification Handlers

- **blockConnected**: `SyncTransaction` for each tx → wallet tracking
- **blockDisconnected**: `SyncTransaction` (marks unconfirmed), resolves conflicts per input, calls `AbandonOrphanedCoinstakes`
- **AbandonOrphanedCoinstakes**: iterates wallet txns, abandons coinstakes with depth=0 not already abandoned
- **updatedBlockTip**: in 26.x, just `m_best_block_time = GetTime()` — **no wake-on-block**

---

## 14. v28-CORE vs 26.x Differences

| Feature | 26.x | v28-CORE |
|---|---|---|
| Timer guard scope | `static` local (global) | Per-wallet `m_last_coin_stake_search_time` |
| `nSearchTime` init | Garbage | `GetAdjustedTimeSeconds()` |
| Wake-on-block | None | `cv_new_block` + `m_new_block_arrived` |
| Safety bump | Hardcoded 16-20s post-block | Computed `m_safety_bump_sleep_ms` |
| Short-circuit sleep | None | Reads `m_safety_bump_sleep_ms` before CreateNewBlock |
| `pos_timio` formula | Same (500 + 30√UTXOs) | Same |
| Difficulty adjustment | Per-block EMA (same formula) | Per-block EMA (same formula) |
| BIP94 | Not active | Not active (regtest only) |
| Input combining guard | `GetHash() != prevout.hash` (buggy) | `COutPoint(...) != prevout` (fixed June 29) |

**Difficulty adjustment note:** Both v26.x and v28-CORE use the same per-block exponential moving average (EMA) formula in `CalculateNextTargetRequired` (`pow.cpp:54`). The difficulty is recalculated at every block boundary, not at 15-block interval boundaries. `nTargetTimespan = 16 * 60` (16 minutes) defines the smoothing window for the EMA, not a fixed adjustment interval. The formula: `bnNew *= ((nInterval-1)*nTargetSpacing + nActualSpacing*2) / ((nInterval+1)*nTargetSpacing)` where `nInterval = 15` and `nActualSpacing` is the time between the last two PoS blocks.

---

## 15. Efficiency Analysis (26.x)

v26.x is **CPU-light but latency-heavy**:
- Timer guard filters 10/11 `CreateNewBlock` calls per window (only ~1 passes)
- Polling every `pos_timio` (~1391ms) without wake-on-block
- `AvailableCoinsForStaking` scans entire `mapWallet` per `CreateCoinStake` call
- Static guard serializes multi-wallet

v28-CORE improves with wake-on-block, per-wallet guard, and precise boundary-aligned sleeps.

### Timer guard timing

```
t=0       CreateNewBlock #1: nSearchTime = garbage_boundary(0) = 0
           guard: 0 > 1.7B? FALSE → fPoSCancel=true
           SleepStaker(1391ms)
...
t≈13910   CreateNewBlock #11: wall clock crossed boundary
           nSearchTime = boundary(16000) = 16000
           guard: 16000 > 0? TRUE → PASSES!
           nLastCoinStakeSearchTime = 16000
```

PoSMiner has zero awareness of the 16s window. It blindly polls every `pos_timio`. The guard in `CreateNewBlock` filters 10/11 calls.

---

## 16. Thread Lifecycle

### Thread hierarchy
```
main → StakeCoins(true) → std::thread(ThreadStakeMiner, pwallet)
  → ThreadStakeMiner()
    → while (true) { try { PoSMiner(pwallet); break; } catch(...) { log; } }
```

### States
| State | Thread? | `m_enabled_staking` | `m_stop_staking_thread` |
|---|---|---|---|
| Wallet loaded, staking off | ❌ | false | false |
| Staking enabled | ✅ | true | false |
| Staking disabled (joining) | ⏳ Joining... | false | true |
| After stop | ❌ | false | false |

`StopStake` blocks until thread finishes. Worst-case join: ~1.4s (one `pos_timio`). Typical: <100ms.

### PoSMiner pauses, thread stays alive

When wallet locked / no peers / IBD / not synced: thread stays alive (blocked in `SleepStaker`). PoSMiner loops in `SleepStaker(5000/10000)` chunks. Thread exits only when `m_stop_staking_thread=true`.

---

## 17. Known Bugs

### Bug #2 — PoSMiner keypool exit leaves `m_enabled_staking=true`

**File:** `miner.cpp:830`

When the keypool is exhausted, `PoSMiner` does a plain `return`. But `m_enabled_staking` stays `true` and the `ThreadStakeMiner` retry loop only catches exceptions — a bare `return` exits the thread permanently. The wallet UI shows staking as enabled but no thread is running.

**To fix:** either set `m_enabled_staking=false` before returning, or convert the `return` to a `throw` so the retry loop catches it.
