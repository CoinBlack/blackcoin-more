# Staker Timing Refactor

**Branch:** v28-CORE
**Date:** 2026-06-16
**Status:** COMPLETED. Applied to the codebase, unifying sleep boundary calculations in miner.cpp and removing tight-loops.

---

## Background

The current staking timing logic splits responsibility across two files:

1. **`wallet.cpp`** (`updatedBlockTip`): pre-calculates sleep duration based on MTP, stores in `m_safety_bump_sleep_ms`
2. **`miner.cpp`** (`PoSMiner` / `CreateNewBlock`): consumes the pre-calculated sleep, has a fallback safety bump

This refactor collapses all sleep/timing responsibility into `miner.cpp`. The wallet's `updatedBlockTip()` becomes a pure wake-up signal.

## Design

| Question | Decision |
|---|---|
| MTP awareness | `MsUntilNextWindow()` takes `mtp` and advances `nextBoundary` past it |
| `pos_timio` fate | Keep as floor: `max(MsUntilNextWindow(...), pos_timio)` |
| Regtest | No special cap; `cv_new_block` wake handles it correctly |

### New helper: `MsUntilNextWindow()`

Add near `UpdateTime()` in `src/node/miner.cpp`:

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

No defensive cap. The loop advances exactly one window per iteration. For MTP to produce a pathological sleep, consensus would first have to accept a chain of 11+ consecutive max-future-drift blocks.

## Code Changes

### 1. `src/wallet/wallet.h` — Remove field

Remove:
```cpp
std::atomic<int64_t> m_safety_bump_sleep_ms{0};
```

### 2. `src/wallet/wallet.cpp` — Simplify `updatedBlockTip()`

**Current** (wallet.cpp:1586-1623):
```cpp
void CWallet::updatedBlockTip()
{
    m_best_block_time = GetTime();
    if (chain().isInitialBlockDownload()) { return; }

    if (chain().getTip()) {
        int64_t nNewMTP = chain().getTip()->GetMedianTimePast();
        int64_t nNextWindow = ((nNewMTP + 16 + 15) / 16) * 16;
        int64_t nowAdjusted = GetAdjustedTimeSeconds();
        int64_t sleepMs = (nNextWindow - nowAdjusted) * 1000;
        if (sleepMs > 16000) { sleepMs %= 16000; if (sleepMs == 0) sleepMs = 16000; }
        if (sleepMs > 0) { m_safety_bump_sleep_ms = sleepMs; }
        else { m_safety_bump_sleep_ms = 0; }
    }

    { std::lock_guard<std::mutex> lock(cv_block_mutex); m_new_block_arrived.store(true); }
    cv_new_block.notify_one();
}
```

**New:**
```cpp
void CWallet::updatedBlockTip()
{
    m_best_block_time = GetTime();
    if (chain().isInitialBlockDownload()) { return; }

    { std::lock_guard<std::mutex> lock(cv_block_mutex); m_new_block_arrived.store(true); }
    cv_new_block.notify_one();
}
```

Rationale: The wallet should only wake the staker. All boundary and MTP math moves entirely to `miner.cpp`.

### 3. `src/node/miner.cpp` — `CreateNewBlock()`

- **Add** `MsUntilNextWindow()` helper.
- **Remove** the entire safety bump block (the `nSafetyBumpSleepMs` variable, the while loop advancing `txCoinStake.nTime`, the modulo 16000 strip).
- **Remove** the `m_safety_bump_sleep_ms` write-back on `fPoSCancel`:

**Current:**
```cpp
if (*pfPoSCancel) {
    if (nSafetyBumpSleepMs > 0) { pwallet->m_safety_bump_sleep_ms = nSafetyBumpSleepMs; }
    return nullptr;
}
```

**New:**
```cpp
if (*pfPoSCancel) {
    return nullptr; // peercoin: no point continuing if we failed to create coinstake
}
```

The timer guard (`nSearchTime > m_last_coin_stake_search_time`) is kept unchanged. It alone prevents re-searching the same 16-second window.

**Remove** initialization guard:
```cpp
if (pwallet->m_last_coin_stake_search_time == 0) {
    pwallet->m_last_coin_stake_search_time = GetAdjustedTimeSeconds();
}
```
(The timer guard uses `nSearchTime > m_last_coin_stake_search_time`, initialized from `txCoinStake.nTime`. On first real attempt, `nSearchTime > 0` passes naturally.)

**Update ghost-block comment:**
```cpp
// Ghost block: valid kernel found but timestamp <= MTP.
// The timer guard and boundary-aligned sleeps in PoSMiner()
// prevent this under normal operation. If this fires, it
// indicates a bug in MsUntilNextWindow() or the timer guard logic.
```

### 4. `src/node/miner.cpp` — `PoSMiner()`

**Remove** the short-circuit consumer (reads `m_safety_bump_sleep_ms.exchange(0)` before `CreateNewBlock()`).

**Replace sleep paths:**

| Path | Old | New |
|---|---|---|
| `fPoSCancel` fallback | `safetyBumpSleep > 0 ? safetyBumpSleep : pos_timio` | `std::max(MsUntilNextWindow(...), (int64_t)pos_timio)` |
| Post-success rest | `(16 + rand(4)) * 1000` | `MsUntilNextWindow(..., newTip->MTP)` |
| Non-PoS fallback | `pos_timio` | Keep as-is |

**Cast required:** `pos_timio` is `unsigned int`. Use `static_cast<int64_t>(pos_timio)` in `std::max<int64_t>` to avoid signed/unsigned warnings.

**No jitter** in post-success sleep. Stake collision avoidance is handled at the kernel level (stake modifier), not by timing randomness.

### 5. `src/node/miner.h`

No changes. `DEFAULT_STAKETIMIO` is kept because `pos_timio` remains as a sleep floor.

## Risk Summary

| Risk | Mitigation |
|---|---|
| Staker attempts search at `nTime <= MTP` | `MsUntilNextWindow()` advances past MTP |
| Large wallets consume too much CPU | `pos_timio` floor preserved |
| Time jumps backward | `std::max(0LL, ...)` prevents negative sleep |
| Long sleeps under weird mock time | No cap — protocol/chain validation keeps MTP sane |
| `m_safety_bump_sleep_ms` dangling references | Only 3 source files used it — all handled |
| Extra `LOCK2` per block wake (no short-circuit) | Timer guard prevents redundant work; accepted trade-off |
| Signed/unsigned mismatch in `std::max` | `static_cast<int64_t>(pos_timio)` |

## Verification

1. `make check` — unit tests pass
2. Regtest staker — verify `PoSMiner` wakes/sleeps at 16-second boundaries
3. `getstakinginfo` — verify search-interval reports correctly
4. `debug.log` — no `"Short-circuit safety bump"` or `"Safety Bump fallback triggered"` messages. Expect `"Minter: No coinstake yet, sleeping ... ms until next window"`.

## Doc Updates

- `agent/SafetyBump.md`: Full rewrite describing single-responsibility design (no `m_safety_bump_sleep_ms`, `updatedBlockTip()` is pure wake signal, `PoSMiner()` computes boundary-aligned sleeps via `MsUntilNextWindow()`, timer guard prevents re-search, `pos_timio` as CPU floor).
- `agent/staking.md` §11: Remove `m_safety_bump_sleep_ms` references, describe `MsUntilNextWindow()` and its MTP-awareness.
- `agent/staking_probabilities.md` §9: Rewrite safety bump bullet.
