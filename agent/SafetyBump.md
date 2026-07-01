# Staker Timing and Wake/Sleep Design

This document describes the staking timing logic in Blackcoin More, which uses a single-responsibility design for computing sleep boundaries (applied via the Staker Timing Refactor).

---

## Overview

Three mechanisms ensure the staker sleeps correctly and searches each 16-second protocol window exactly once:

1. **`updatedBlockTip()`** — Pure wake signal. Sets `m_new_block_arrived = true` and notifies `cv_new_block`.
2. **`MsUntilNextWindow()`** — Core timing primitive in `miner.cpp` that calculates the milliseconds until the next 16-second window, strictly advancing past the chain's Median Time Past (MTP).
3. **Timer guard** — Per-wallet timestamp barrier (`m_last_coin_stake_search_time`) inside `CreateNewBlock()` that prevents re-searching the same 16-second window.

---

## 1. `updatedBlockTip()` — Pure Wake Signal

**File:** `src/wallet/wallet.cpp`

```cpp
void CWallet::updatedBlockTip()
{
    m_best_block_time = GetTime();

    if (chain().isInitialBlockDownload()) return;

    {
        std::lock_guard<std::mutex> lock(cv_block_mutex);
        m_new_block_arrived.store(true);
    }
    cv_new_block.notify_one();
}
```

- Updates `m_best_block_time` for wallet rebroadcast scheduling.
- Ignores notifications during IBD.
- Wakes the staker thread via condition variable `cv_new_block`. 
- **No timing math**: The wallet does not attempt to calculate sleep durations or boundaries. It only wakes the staker.

---

## 2. Wake/Sleep Primitive

**File:** `src/node/miner.cpp`

```cpp
bool SleepStaker(CWallet *pwallet, uint64_t milliseconds) {
    std::unique_lock<std::mutex> lock(pwallet->cv_block_mutex);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);

    if (pwallet->m_new_block_arrived.exchange(false)) return true;

    while (std::chrono::steady_clock::now() < deadline) {
        if (pwallet->IsStakeClosing()) return false;

        auto result = pwallet->cv_new_block.wait_until(lock, deadline);

        if (result == std::cv_status::no_timeout) {
            if (pwallet->m_new_block_arrived.exchange(false)) return true;
        }
    }

    return !pwallet->IsStakeClosing();
}
```

Sleeps for the requested duration unless a new block arrives (`cv_new_block` notified) or the staker is shutting down (`IsStakeClosing()`). It is the only sleep primitive used by `PoSMiner()`.

---

## 3. `MsUntilNextWindow()` — Boundary Calculation

**File:** `src/node/miner.cpp`

```cpp
int64_t MsUntilNextWindow(const Consensus::Params& consensus, int64_t mtp)
{
    int64_t now = GetAdjustedTimeSeconds();
    int64_t nextBoundary = (now & ~consensus.nStakeTimestampMask)
                         + (consensus.nStakeTimestampMask + 1);

    // Advance past MTP. Block timestamp must be strictly greater than MTP.
    while (nextBoundary <= mtp)
        nextBoundary += (consensus.nStakeTimestampMask + 1);

    return std::max(0LL, (nextBoundary - now) * 1000);
}
```

This helper handles all staking timing logic. It masks the current time to the active 16-second boundary, advances to the next boundary, and ensures the target timestamp is strictly greater than the chain's Median Time Past (MTP).

If the MTP is pushed far into the future (e.g. by a malicious peer), this function will return a very large sleep duration. This is safe and intended: the staker cannot possibly create a valid block until that time passes anyway. If the chain reorganizes to a normal MTP, `updatedBlockTip()` fires and instantly wakes the staker out of its sleep via `cv_new_block`.

---

## 4. Timer Guard

**File:** `src/node/miner.cpp`

```cpp
int64_t nSearchTime = txCoinStake.nTime;

if (nSearchTime > pwallet->m_last_coin_stake_search_time) {
    if (nSearchTime - pindexPrev->GetMedianTimePast() < 2) {
        LogPrint(BCLog::COINSTAKE, "[%s] WARNING: Close MTP collision detected...\n", ...);
    }

    if (wallet::CreateCoinStake(*pwallet, pblock->nBits, 1, txCoinStake, nFees, destination)) {
        // ... coinstake found ...
    }
    pwallet->m_last_coin_stake_search_interval = nSearchTime - pwallet->m_last_coin_stake_search_time;
    pwallet->m_last_coin_stake_search_time = nSearchTime;
}
```

The timer guard (`nSearchTime > m_last_coin_stake_search_time`) is the ultimate barrier against tight-looping. Even if `cv_new_block` is spurred or the thread wakes early, the guard blocks `CreateCoinStake()` from being called twice for the same `nTime` timestamp.

---

## 5. `PoSMiner()` Sleep Paths

**File:** `src/node/miner.cpp`

### 5a. Failed coinstake (`fPoSCancel`)

```cpp
if (fPoSCancel == true)
{
    int64_t sleepTime = std::max(MsUntilNextWindow(Params().GetConsensus(), pindexPrev->GetMedianTimePast()), static_cast<int64_t>(pos_timio));

    if (!SleepStaker(pwallet, sleepTime))
        return;

    continue;
}
```

When no kernel is found for the current window, the thread sleeps until the next valid boundary (via `MsUntilNextWindow`). 
`pos_timio` acts as a minimum sleep floor. This is critical for preventing **spin-loops and lock contention**: if the thread wakes up spuriously or early, the timer guard prevents it from executing the expensive cryptography, but without a sleep floor, the thread would instantly loop and re-acquire global locks (`cs_main`, `cs_wallet`), maxing out a CPU core. `pos_timio` forces it to yield the CPU for at least a few seconds (scaling with wallet size).

### 5b. After successful block

```cpp
ProcessBlockFound(pblock, pwallet->chain().chainman());
// Rest after successful block
uint64_t stakerRestTime = MsUntilNextWindow(Params().GetConsensus(), pwallet->chain().getTip()->GetMedianTimePast());
if (!SleepStaker(pwallet, stakerRestTime))
    return;
continue;
```

After successfully finding and broadcasting a block, the staker immediately rests until the next valid staking window begins.

### 5c. Pre-Search Pauses (Rescan, IBD, Disconnected)

Before attempting to stake, `PoSMiner` checks for unstable or unsynced states:
- **`IsScanning()`**: If the wallet is actively rescanning the blockchain, UTXOs are in flux. The staker sleeps in 5s intervals.
- **`m_importing` / `m_blockfiles_indexed`**: Pauses during `-reindex` or `-loadblock`.
- **`isInitialBlockDownload()`**: Pauses if the node is catching up to the network tip.
- **`getNodeCount(Both) == 0`**: Pauses if disconnected (no peers to broadcast to).
- **`IsLocked()`**: Pauses if the wallet passphrase is not entered.

These pauses happen *before* any UTXOs are evaluated, ensuring the staker never operates on an inconsistent `mapWallet` or obsolete chain.

---

## 6. Files Involved

| File | Role |
|---|---|
| `src/wallet/wallet.cpp` | `updatedBlockTip()` — pure wake signal |
| `src/wallet/wallet.h` | `cv_new_block`, `cv_block_mutex`, `m_new_block_arrived`, `m_last_coin_stake_search_time` |
| `src/node/miner.cpp` | `MsUntilNextWindow()`, `SleepStaker()`, `CreateNewBlock()` (timer guard), `PoSMiner()` (sleep paths) |
| `src/node/miner.h` | `DEFAULT_STAKETIMIO` |
