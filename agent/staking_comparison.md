# Staking Code Comparison: Blackcoin 132 → 262 → 284 vs Peercoin

## Scope

This document tracks every meaningful difference in the proof-of-stake (PoS) staking
flow between Blackcoin 132, 262, 284, and Peercoin. Version 272 is intentionally
skipped per request — it is largely identical to 262 with refactoring and a few
script-type additions, none of which change the staking process itself.

For each feature, the table below notes whether the version has it, the source
file and line range, and a one-line description. Where a feature was added or
removed across versions, a short "→" arrow shows the migration path.

## File Layout

| Version | Stake kernel code | PoS miner thread | Sign function |
|---------|-------------------|------------------|---------------|
| 132     | `src/wallet/wallet.cpp` (CreateCoinStake) | `src/miner.cpp` (ThreadStakeMiner) | `src/main.cpp` (SignBlock) |
| 262     | `src/wallet/staking.cpp` (CreateCoinStake) | `src/node/miner.cpp` (PoSMiner) | `src/node/miner.cpp` (SignBlock) |
| 284     | `src/wallet/staking.cpp` (CreateCoinStake) | `src/node/miner.cpp` (PoSMiner) | `src/node/miner.cpp` (SignBlock) |
| Peercoin | `src/wallet/wallet.cpp` (CreateCoinStake) | `src/wallet/wallet.cpp` (ThreadStakeMiner) | `src/wallet/wallet.cpp` (SignBlock) |

Blackcoin 262 moved staking out of `src/wallet/wallet.cpp` and `src/miner.cpp`
into `src/wallet/staking.cpp` and `src/node/miner.cpp` as part of the Bitcoin
Core 28.x upstream merge. Peercoin keeps everything in `src/wallet/wallet.cpp`.

---

## 1. Backward Kernel Search

The kernel search loop walks backward in time from the current block's timestamp
to find a UTXO whose kernel hash meets the target. The loop has two parameters:
`nSearchInterval` (passed in by the caller) and `nMaxStakeSearchInterval` (a
hard cap inside CreateCoinStake).

### 1.1 `nMaxStakeSearchInterval` cap

| Version | Has it? | Value | Location |
|---------|---------|-------|----------|
| 132     | Yes | 60 | `src/wallet/wallet.cpp:726` |
| 262     | Yes | 60 | `src/wallet/staking.cpp:310` |
| 284     | Yes | 60 | `src/wallet/staking.cpp:310` |
| Peercoin | Yes | 60 | `src/wallet/wallet.cpp:3671` |

The cap is inherited from Peercoin and is identical in all four implementations.
The cap is never reached in any Blackcoin version because `nSearchInterval = 1`
is passed in (see 1.2). It is reachable in Peercoin.

### 1.2 `nSearchInterval` passed in by caller

| Version | Value | Caller | Notes |
|---------|-------|--------|-------|
| 132     | 1 | `SignBlock` at `src/main.cpp:3768` | Always 1. Never uses Peercoin's dynamic `nSearchTime - nLastCoinStakeSearchTime`. |
| 262     | 1 | `CreateNewBlock` at `src/node/miner.cpp:234` | Same as 132. |
| 284     | 1 | `CreateNewBlock` at `src/node/miner.cpp:246` | Same. Kept despite the timer-guard refactor in commit `57d605d415` (Jul 2026) which explicitly states "search once every 16 seconds. it is no use to keep searching. it only wastes CPU cycles". |
| Peercoin | `nSearchTime - nLastCoinStakeSearchTime` (capped at 60) | `CreateNewBlock` at `src/node/miner.cpp:178` | Dynamic — the search range is how long ago the last search ran. This is the original Peercoin design. |

**Blackcoin never uses Peercoin's dynamic `nSearchInterval`.** All three Blackcoin
versions hardcode `1`, limiting the effective search range to `std::min(1, 60) = 1`
second. The 60-second cap is dead code in Blackcoin.

Historical: Blackcoin 132 inherited Peercoin's `nSearchTime - nLastCoinStakeSearchTime`
formula at the original 2017 "Blackcoin Lore" fork (`2fdd12b2ea`). The change to
hardcoded `1` was introduced in commit `a10767cc47` (Jan 2023, lateminer) titled
"Use txCoinStake.nTime when staking" as a side effect of timestamp handling
refactor. Kept ever since.

### 1.3 The `txNew.nTime -= n` after-kernel assignment

All four versions use the same line after a kernel is found:

```cpp
if (CheckKernel(pindexPrev, nBits, txNew.nTime - n, prevoutStake, ...)) {
    ...
    txNew.nTime -= n;  // sets the coinstake timestamp to the kernel's n
    ...
}
```

| Version | Location | Behavior when n > 0 |
|---------|----------|---------------------|
| 132     | `src/wallet/wallet.cpp:780` | Result rejected by `CheckCoinStakeTimestamp` (16s boundary). |
| 262     | `src/wallet/staking.cpp:360` | Same. |
| 284     | `src/wallet/staking.cpp:360` | Same, but also rejected by additional MTP check at line 381 in some cases. |
| Peercoin | `src/wallet/wallet.cpp:3758` | Valid (no boundary rule). |

**Latent bug in all Blackcoin versions**: with `nSearchInterval = 1`, only
`n = 0` (boundary) and `n = 1` (off-boundary) are checked. The `n = 1` case
produces a coinstake whose `nTime` fails `CheckCoinStakeTimestamp` at validation
(16s mask check: `(nTimeTx & 0xf) == 0`). In practice this rarely triggers
because the `n = 0` case almost always finds a kernel first for Blackcoin's
low-difficulty PoS, but the bug exists. Peercoin is unaffected because it has
no 16s boundary rule.

**Increasing `nSearchInterval` to 15 would extend the same bug** — 14 of 15
iterations would produce rejected blocks. The 1-second search is the least-worst
option for Blackcoin's 16s protocol.

---

## 2. Race Condition Guard: `pindexBestHeader` Check

### Background: two different "chain tips"

A node can have two different views of "the latest block":

1. **`pindexPrev` = active chain tip.** The block at the end of the chain the
   node currently considers mainnet. Updates only after a block is fully
   validated and connected (`BlockConnected` callback).

2. **`pindexBestHeader` (132) / `m_chainman.m_best_header` (modern) = most-work
   header.** The block header with the most cumulative work that the node has
   heard about. Updates optimistically as soon as a header is received, before
   the block is validated.

These can differ: a peer can send a high-work block on a different branch.
The header is added to the block index and `m_best_header` updates immediately.
The full block validation (script checks, tx verification, connection to
chain) takes longer. Until validation finishes, `m_best_header` points to the
new branch but the active chain tip is still on the old branch.

### The check in 132

The 132 kernel search loop has a third termination condition:

```cpp
for (unsigned int n=0; n<min(nSearchInterval,(int64_t)nMaxStakeSearchInterval)
                     && !fKernelFound
                     && pindexPrev == pindexBestHeader; n++)
```

Re-checked at **every iteration** of the loop. The check means: "if the
most-work header has moved to a different branch while I'm searching, give up
— anything I find would be on a stale fork."

| Version | Has the check? | Location |
|---------|----------------|----------|
| 132     | Yes | `src/wallet/wallet.cpp:727` |
| 262     | No | — |
| 284     | No | — |
| Peercoin | No | — |

### Concrete scenario where the check matters

1. Wallet's active chain tip is block T (work W)
2. Wallet starts a kernel search, captures `pindexPrev = T`
3. Wallet begins iterating `n = 0, 1, 2, ...` to check timestamps
4. Meanwhile, a peer sends a high-work block B on a different branch (work W+1)
5. The header is received. `m_best_header` updates to B's tip. Validation begins.
6. The staking thread is still holding `cs_main` (acquired at `CreateCoinStake:257`).
   Block validation is waiting for `cs_main` to be released.
7. The staking thread continues its search, finds a kernel at `txNew.nTime - n`
8. Builds a coinstake with `hashPrevBlock = T` (the old tip captured in step 2)
9. Releases `cs_main`. Block B's validation proceeds, becomes the new active tip.
10. The staking thread submits its coinstake. Validation sees `hashPrevBlock = T`,
    but the active tip is now B. The block is on a stale fork — rejected.

The check `pindexPrev == pindexBestHeader` in 132 would have aborted at step 7
(because at that point `pindexPrev (T) != pindexBestHeader (B's tip)`). The
thread would have slept and tried again in the next 16-second window. No
wasted CPU on a block destined for a stale fork.

In 262+, the check is gone. The thread wastes CPU on a block that gets
rejected. No consensus impact, no security impact — just wasted work.

### Why 132 had it and 262+ doesn't

The `pindexBestHeader` global existed in 132 (`src/main.cpp:3089`). The kernel
search used it as a defensive optimization to avoid wasted work.

When Blackcoin merged more recent Bitcoin Core upstream code (0.13 → 0.14 era),
the global was replaced by `m_chainman.m_best_header` in the `ChainstateManager`
class. The kernel search loop's check was not migrated — it was just dropped.

Peercoin has never had this check.

### Is the modern version actually vulnerable?

**Not to consensus or security.** The lock ordering
(`LOCK2(cs_main, wallet.cs_wallet)` at `staking.cpp:257`) ensures the chain
state is consistent during the kernel search. Any new block must wait for
`cs_main` to be released before being validated and connected.

**Yes, to wasted CPU.** The lock prevents concurrent modification but not the
optimistic header update. `m_best_header` can move to a different branch
without holding `cs_main`, and the staking thread can complete its search
based on the now-stale `pindexPrev` before the validation finishes.

**Practical impact**: minor. The validation process rejects the stale-fork
block, and the staking thread re-tries. The user doesn't notice anything
except slightly higher CPU usage on nodes that frequently see chain
reorganizations.

### Restoring the check in 284

A modern equivalent of the 132 check would be:

```cpp
for (unsigned int n = 0;
     n < std::min(nSearchInterval, (int64_t)nMaxStakeSearchInterval) && !fKernelFound
     && pindexPrev == m_chainman.m_best_header;
     n++)
```

This restores the defensive optimization using modern APIs. It is not a
correctness fix — the current code is correct. It is a CPU-saving optimization
for nodes that frequently experience chain reorganizations during staking.

---

## 3. Timestamp Validation

After finding a kernel, the coinstake timestamp must be validated against the
previous block's median time past (MTP).

| Version | Where | What is checked |
|---------|-------|-----------------|
| 132     | `SignBlock` at `src/main.cpp:3770` | `txCoinStake.nTime >= pindexBestHeader->GetPastTimeLimit() + 1` |
| 262     | `CreateCoinStake` at `src/wallet/staking.cpp` (line ~345) | No MTP check inside CreateCoinStake; caller checks at `src/node/miner.cpp:236` |
| 284     | `CreateCoinStake` at `src/wallet/staking.cpp:381` | `txNew.nTime < pindexPrev->GetMedianTimePast() + 1` |
| Peercoin | None in CreateCoinStake | Caller checks at `src/node/miner.cpp:180` |

**Differences:**

- **132** uses `pindexBestHeader->GetPastTimeLimit()` (most-work header) for the
  timestamp check. `GetPastTimeLimit()` returns the MTP of the most-work chain.
- **262** removed the in-loop check entirely; the caller (PoSMiner) checks
  `txCoinStake.nTime >= pindexPrev->GetMedianTimePast() + 1` after `CreateCoinStake`
  returns.
- **284** re-introduced the check inside `CreateCoinStake` but uses
  `pindexPrev->GetMedianTimePast()` (active chain tip) instead of
  `pindexBestHeader->GetPastTimeLimit()`.
- **Peercoin** has no in-CreateCoinStake check; the caller checks at miner.cpp:180.

The 132 check is the most defensive (uses the most-work chain). The 284 check
is functionally equivalent in practice (active tip usually is the most-work tip
once the staking thread has `cs_main`). The 262 check is correct but coarser —
the caller checks after the fact, so an invalid coinstake is built and discarded
rather than rejected during kernel search.

The 284 check at `staking.cpp:381` is a deliberate "fail fast" addition that
prevents the caller from building an invalid coinstake.

---

## 4. OP_RETURN Carrier with Timestamp

Modern Blackcoin coinstakes include an `OP_RETURN` output that carries the
SignKey pubkey and a 4-byte little-endian timestamp.

| Version | Has it? | Location |
|---------|---------|----------|
| 132     | No | — |
| 262     | No | — |
| 284     | Yes | `src/wallet/staking.cpp:351-373` |
| Peercoin | No | — |

**284 only.** The carrier is built as:

```cpp
scriptCarrier = CScript() << OP_RETURN << ToByteVector(minterPubKey);
uint32_t nTime = txNew.nTime;
unsigned char timeBytes[4] = { ... };
scriptCarrier << std::vector<unsigned char>(timeBytes, timeBytes + 4);
txNew.vout.push_back(CTxOut(0, scriptCarrier));  // vout[1]
```

### Why it was added (v2 transaction serialization)

This is **not** an optimization — it is a **correctness fix** for v2 transactions.

The v2 transaction format (`primitives/transaction.h:230-236`) does **not**
serialize the `nTime` field on the wire:

```cpp
s >> tx.version;
if (tx.version < 2)
    s >> tx.nTime;
else
    tx.nTime = 0;  // reconstructed from block header at coins.cpp:129
```

For v1 transactions, the `nTime` field was part of the serialization and
contributed to the txid computation. Two coinstakes with the same kernel/reward
but different `nTime` would have different txids.

For v2 transactions, the `nTime` is **not** part of the txid. Two coinstakes
with the same kernel, same reward, and same outputs would produce the **same
txid** — even if they have different timestamps.

**The OP_RETURN carrier solves this** by embedding the timestamp in the
coinstake's outputs (vout[1]). Since the output set is part of the txid, the
embedded timestamp makes each coinstake unique per 16-second window.

Without the carrier:
- Two miners using the same kernel in the same window would produce identical
  coinstakes (same txid)
- The second instance would be rejected as a duplicate
- Staking would be effectively single-miner per window
- Network could not distinguish between attempts

### Why 16-second uniqueness matters

Blackcoin's 16-second mask (`nStakeTimestampMask = 0xf`) means multiple miners
may find valid kernels within the same 16-second window. The carrier ensures
that each miner's coinstake has a unique txid, allowing multiple competing
coinstakes in the same window.

### Consensus vs. defensive data

The 4-byte timestamp in the carrier is **not consensus-checked**. The
`CheckBlockSignature` function (validation.cpp:3860-3897) reads only the first
two `GetOp` calls from the OP_RETURN (pubkey + lookahead for verification).
The timestamp bytes are not validated against the block's `nTime` or
`txNew.nTime` — they only serve to disambiguate the txid.

The 4-byte timestamp matches the coinstake's actual `nTime` (which is
reconstructed from the block header at validation). If they didn't match, the
mempool would still accept the transaction (since the consensus check is on
the block-level `nTime`, not the carrier bytes).

### Wire format impact

Adds ~37 bytes to each coinstake (33-byte pubkey push + 4-byte timestamp push
+ OP_RETURN opcode). Per-block overhead is ~1.4 KB over a year of staking at
one block per 16 seconds.

---

## 5. nTimeSmart Input Sanity Check

When combining additional inputs into a coinstake, the wallet must ensure the
input's block time is not later than the coinstake's timestamp (would fail
`bad-txns-time-earlier-than-input` validation).

| Version | Has it? | Location |
|---------|---------|----------|
| 132     | No | — (combining logic only checks value and address) |
| 262     | No | — |
| 284     | Yes | `src/wallet/staking.cpp:433-434` |
| Peercoin | No | — |

**284 only.** The check is:

```cpp
if (pcoin.first->nTimeSmart > txNew.nTime)
    continue;
```

`nTimeSmart` is the block time for confirmed transactions (or `nTimeReceived`
for unconfirmed). The check uses `nTimeSmart` instead of `tx->nTime` because
v2 transactions don't serialize `nTime` (it's reconstructed from the block
header via `coins.cpp:129`).

This prevents the wallet from combining an input whose block time is later than
the coinstake's timestamp, which would fail consensus validation.

**Wire format impact**: none. Just a sanity check that prevents wasted work.

---

## 6. MinterKey Abstraction (SignKey in Blackcoin, mintkey in Peercoin)

A dedicated P2PKH address whose private key is used to sign blocks and (in
Blackcoin 284) to embed a minter identifier in the OP_RETURN carrier.

| Version | Has it? | Label | AddressPurpose | Used for |
|---------|---------|-------|----------------|----------|
| 132     | No | — | — | — |
| 262     | No | — | — | — |
| 284     | Yes (as "SignKey") | "SignKey" | `AddressPurpose::SIGNKEY` | All kernel types; signer for block; pubkey embedded in OP_RETURN carrier |
| Peercoin | Yes (as "mintkey") | "mintkey" | None (RECEIVE/SEND/REFUND only) | Taproot kernels only; signer for block (P2TR reward output) |

**All three modern versions** (262, 284, Peercoin) have some form of this
abstraction. Blackcoin 262 does not — the minter's signing key is selected
from the wallet's keypool ad-hoc, not stored as a named address book entry.

### Blackcoin 284 SignKey

```cpp
// src/node/miner.cpp:723-746
const std::string label = "SignKey";
pwallet->ForEachAddrBookEntry([&](const CTxDestination& _dest, const std::string& _label, bool _is_change, const std::optional<wallet::AddressPurpose>& _purpose) {
    if (_is_change) return;
    if (_label == label && _purpose && *_purpose == wallet::AddressPurpose::SIGNKEY) {
        dest = _dest;
    }
});

if (!std::get_if<CNoDestination>(&dest) && !std::holds_alternative<PKHash>(dest)) {
    pwallet->DelAddressBook(dest, /*force=*/true);
    dest = CNoDestination();
}

if (std::get_if<CNoDestination>(&dest)) {
    auto op_dest = pwallet->GetNewDestination(OutputType::LEGACY, label);
    dest = *op_dest;
    pwallet->SetAddressBook(dest, label, wallet::AddressPurpose::SIGNKEY);
}
```

Features specific to 284:
- **Self-healing guard** (lines 734-737): if the stored SignKey entry has
  somehow become a non-P2PKH destination (corruption or legacy entry), it is
  deleted and a new one is created. Prevents the coinstake construction from
  failing due to a malformed address.
- **Explicit `AddressPurpose::SIGNKEY`** (line 745): distinguishes the
  SignKey from regular receive/send addresses. Allows the address book to
  contain multiple addresses with the label "SignKey" if needed.
- **OP_RETURN carrier** (see §4): the SignKey pubkey is embedded in
  `OP_RETURN <pubkey> [timestamp]` for txid disambiguation.

### Peercoin mintkey

```cpp
// src/node/miner.cpp:566-581
const std::string label = "mintkey";
pwallet->ForEachAddrBookEntry([&](const CTxDestination& _dest, const std::string& _label, bool _is_change, const std::optional<wallet::AddressPurpose>& _purpose) {
    if (_is_change) return;
    if (_label == label)
        dest = _dest;
});

if (std::get_if<CNoDestination>(&dest)) {
    auto op_dest = pwallet->GetNewDestination(OutputType::LEGACY, label);
    dest = *op_dest;
}
```

Differences from Blackcoin 284:
- **No `AddressPurpose`** — Peercoin's address book has only
  `RECEIVE`/`SEND`/`REFUND` purposes. The mintkey is matched by label only.
- **No self-healing guard** — if the stored mintkey is corrupted, the code
  would still find it and use the corrupted destination.
- **No OP_RETURN carrier** — Peercoin coinstakes don't have the carrier
  pattern. The mintkey pubkey is only used for Taproot kernel reward
  construction, not for txid disambiguation.

### Where the mintkey/SignKey is used

**Blackcoin 284** (`staking.cpp:331-348`):
- All kernel types (P2PK, P2PKH, P2WPKH, P2TR)
- Used to look up the SignKey pubkey for the OP_RETURN carrier
- Also used as the "destination" parameter in `CreateCoinStake` for kernel
  resolution

**Peercoin** (`wallet.cpp:3737-3756`):
- **Taproot kernels only** (`TxoutType::WITNESS_V1_TAPROOT`)
- Used to derive the scriptPubKey for an extra "minter key" output
- Sets `bMinterKey = true`, which adds an extra output to the coinstake
  (line 3763-3768)

For other kernel types, Peercoin's code uses the kernel's own script (P2PK)
or converts P2PKH → P2PK inline (line 3716). Blackcoin 284 is more uniform:
all kernel types use the SignKey as the carrier and convert legacy P2PK
rewards to P2PKH automatically (see §13).

### 132 vs 262 vs 284

**132** does not have a named SignKey/mintkey. The minter key is selected
from the wallet's keypool via `CReserveKey reservekey(pwallet)` in
`ThreadStakeMiner` (`src/miner.cpp:580`). The key changes per block but
is not stored as a named address.

**262** also does not have a named SignKey. The minter's signing key is
passed to `CreateCoinStake` as part of the wallet's keys. Blackcoin 262
and Peercoin both have similar minter key handling but no dedicated
address book entry.

**284** introduced the SignKey with `AddressPurpose::SIGNKEY` as part of
the `57d605d415` commit (Jul 2026).

### Block signature (vchBlockSig)

In all modern versions, the block is signed by the minter's key:
- 132: `key.Sign(block.GetHash(), block.vchBlockSig, 0)` (minter from keypool)
- 262: Same
- 284: `key.Sign(block.GetHash(), block.vchBlockSig, 0)` (minter is SignKey)
- Peercoin: Same as 132 (minter from keypool or mintkey)

---

## 7. 16-Second Timestamp Mask (V2 Protocol)

All coinstake timestamps must be on 16-second boundaries (`nTime & 0xf == 0`).

| Version | Has it? | Mask value | Check location |
|---------|---------|------------|----------------|
| 132     | Yes | `0xf` | `src/pos.cpp:44-50` |
| 262     | Yes | `0xf` | `src/pos.cpp:44-50` |
| 284     | Yes | `0xf` | `src/pos.cpp:44-50` |
| Peercoin | No | n/a | No boundary check |

**Blackcoin only.** The check is `CheckCoinStakeTimestamp(nTimeBlock, nTimeTx)`
in `pos.cpp`:

```cpp
if (params.IsProtocolV2(nTimeBlock))
    return (nTimeBlock == nTimeTx) && ((nTimeTx & params.nStakeTimestampMask) == 0);
```

This is the rule that makes the latent `n > 0` bug (see §1.3) produce rejected
blocks. Peercoin has no boundary rule, so its `txNew.nTime -= n` always produces
a valid coinstake.

---

## 8. Stake Combine (P2PKH/P2WPKH/P2TR Input Combining)

When finding additional inputs to combine into a coinstake, the wallet matches
inputs by script type.

| Version | Same-type combining? | P2PKH-kernel-P2PK-input combining? | Split threshold |
|---------|---------------------|-----------------------------------|-----------------|
| 132     | Yes | No | `GetStakeCombineThreshold()` (~250 BLK) and `GetStakeSplitThreshold()` (~500 BLK) |
| 262     | Yes | No | Same |
| 284     | Yes | **Yes** (`combineP2PK`) | Same; uses `GetStakeCombineThreshold()` |
| Peercoin | Yes | No | Same |

**132/262/Peercoin**: only inputs with the same `scriptPubKey` as the kernel
are combined. For P2PKH kernels, only P2PKH inputs are combined. Old P2PK
rewards are missed.

**284**: P2PKH kernels additionally accept P2PK inputs that match the current
SignKey pubkey. This sweeps legacy P2PK rewards into new P2PKH coinstakes.
Implemented in `staking.cpp:404-414` with the `combineP2PK` flag.

**Wire format impact**: 284 coinstakes may include P2PK inputs in the
combining loop, which would not have happened in 132/262. Old P2PK rewards are
now actively swept.

---

## 9. Stake Cache

The wallet caches kernel checks to avoid redundant disk reads.

| Version | Has it? | Default | Active? |
|---------|---------|---------|---------|
| 132     | Yes | Off (DEFAULT_STAKE_CACHE=false) | Disabled by default, opt-in via `-stakecache` |
| 262     | Field exists in `wallet.h` but not used in `staking.cpp` | — | Field present, no active usage |
| 284     | Yes | **On** (commented in staking.cpp:288 says "leave disabled by default" but `-stakecache` is enabled) | Active |
| Peercoin | No | — | — |

**132** has the stake cache field and basic logic but defaults to off. The
cache is a `std::map<COutPoint, CStakeCache>` keyed by UTXO.

**262** has the field in `wallet.h` (line 328) but does not actively use it in
`staking.cpp`. The cache sits unused.

**284** has the field and uses it actively. Commit `57d605d415` (Jul 2026) "activate
stakecache correctly, but leave disabled by default" was the relevant change,
though the default behavior in the code suggests the cache is now used when
`-stakecache` is set (default is `node::DEFAULT_STAKE_CACHE`).

**Peercoin** does not have a stake cache.

**Impact**: the cache saves ~2 disk reads per UTXO per kernel check, reducing
block creation time from ~100s to <100ms in 284.

---

## 10. `nStakeMinAge` Coin-Age Requirement (Peercoin only)

Peercoin requires coins to be at least `nStakeMinAge` (30 days on mainnet, 1
day on testnet) old before they can be used as stake kernels. This encourages
holding.

| Version | Has it? |
|---------|---------|
| 132     | No |
| 262     | No |
| 284     | No |
| Peercoin | Yes (30 days mainnet, 1 day testnet) |

**Peercoin only.** Blackcoin dropped this requirement when it forked from
Peercoin. The only maturity requirement in Blackcoin is `nCoinbaseMaturity`
(500 blocks, ~2.2 hours on mainnet), which is also in Peercoin.

The original Peercoin check at `src/wallet/wallet.cpp:3672` is:

```cpp
if (header.GetBlockTime() + params.nStakeMinAge > txNew.nTime - nMaxStakeSearchInterval)
    continue; // only count coins meeting min age requirement
```

Blackcoin removed this when it simplified the staking logic.

---

## 11. Donation Output (Blackcoin 132 only)

132 had a built-in dev-fund donation feature.

| Version | Has it? | Percentage |
|---------|---------|------------|
| 132     | Yes | `-donationpercentage` arg, default 0 |
| 262     | No | — |
| 284     | No | — |
| Peercoin | No | — |

**132 only.** The donation was a percentage of the staking reward sent to a
configured dev fund address. This was removed in 262 when the `-donationpercentage`
argument and `nDonationPercentage` variable were removed.

**Impact**: the dev fund feature is no longer present. Block rewards go entirely
to the minter (subject to the split threshold).

---

## 12. Block Signature (vchBlockSig)

All Blackcoin coinstake blocks include a block signature in `vchBlockSig`.

| Version | Has it? | Signing logic |
|---------|---------|----------------|
| 132     | Yes | `SignBlock(*pblock, *pwallet, nFees)` with `key.Sign(block.GetHash(), block.vchBlockSig, 0)` |
| 262     | Yes | `SignBlock(*pblock, *pwallet)` with `key.Sign(block.GetHash(), block.vchBlockSig, 0)` |
| 284     | Yes | `SignBlock(*pblock, *pwallet)` with `key.Sign(block.GetHash(), block.vchBlockSig, 0)` |
| Peercoin | Yes | Same |

**All four versions.** The signature scheme is the same: ECDSA over the block
hash, using the SignKey (Blackcoin 284) or wallet key (132, 262, Peercoin).

**Wire format impact**: ~70 bytes per block (DER-encoded ECDSA signature).

---

## 13. P2PK Reward Output (Legacy)

Old-style coinstakes produced a P2PK output for the reward (just `<pubkey> OP_CHECKSIG`).

| Version | P2PK reward output? | How legacy P2PK rewards are handled in combining |
|---------|---------------------|---------------------------------------------------|
| 132     | Yes (P2PK only) | Not combined (script mismatch) |
| 262     | Yes (P2PK only) | Not combined (script mismatch) |
| 284     | **No** (upgraded to P2PKH) | Swept via `combineP2PK` flag (see §8) |
| Peercoin | Yes (P2PK only) | Not combined (script mismatch) |

**132/262/Peercoin**: P2PK kernels produce P2PK reward outputs.

**284**: P2PK kernels are automatically upgraded to P2PKH reward outputs:
```cpp
if (whichType == TxoutType::PUBKEY) {
    scriptPubKeyOut = GetScriptForDestination(PKHash(CPubKey(vSolutions[0])));
}
```

This standardizes the reward output type. The legacy P2PK outputs from old
coinstakes are still swept into new coinstakes via the `combineP2PK` logic
(see §8).

**Wire format impact**: 284 coinstakes always produce P2PKH rewards. P2PK
rewards only exist in old coinstakes (pre-284).

---

## 14. Lock Ordering

The order in which `cs_main` and `cs_wallet` are acquired during the staking
flow is critical to avoid deadlocks.

| Version | Order | Location |
|---------|-------|----------|
| 132     | `LOCK2(cs_main, wallet.cs_wallet)` (cs_main first) | `src/wallet/wallet.cpp:683` |
| 262     | `LOCK2(cs_main, wallet.cs_wallet)` (cs_main first) | `src/wallet/staking.cpp:257` |
| 284     | `LOCK2(cs_main, wallet.cs_wallet)` (cs_main first) | `src/wallet/staking.cpp:257` |
| Peercoin | `LOCK2(cs_main, pwallet->cs_wallet)` (cs_main first) | `src/wallet/wallet.cpp:3607` |

**All four versions.** Consistent lock ordering. Acquire `cs_main` before
`cs_wallet` to avoid deadlocks with other threads.

**Note**: in 132, the `CreateCoinStake` call is wrapped in `LOCK2(cs_main, mempool.cs)`
in `CreateNewBlock` (`src/miner.cpp:580`). The wallet lock is acquired inside
`CreateCoinStake`. In 262+, the `LOCK2(cs_main, wallet.cs_wallet)` is at the
top of `CreateCoinStake`, and `CreateNewBlock` acquires `cs_main` separately.

---

## 15. Summary Table

| Feature | 132 | 262 | 284 | Peercoin |
|---------|:---:|:---:|:---:|:--------:|
| 16s timestamp mask | ✓ | ✓ | ✓ | ✗ |
| `nMaxStakeSearchInterval = 60` cap | ✓ | ✓ | ✓ | ✓ |
| `nSearchInterval = 1` (hardcoded) | ✓ | ✓ | ✓ | ✗ (dynamic) |
| `pindexBestHeader` race guard | ✓ | ✗ | ✗ | ✗ |
| MTP check inside CreateCoinStake | ✗ | ✗ | ✓ | ✗ |
| MTP check in caller (PoSMiner) | ✓ | ✓ | ✓ | ✓ |
| OP_RETURN carrier with timestamp | ✗ | ✗ | ✓ | ✗ |
| nTimeSmart input sanity check | ✗ | ✗ | ✓ | ✗ |
| Minterkey abstraction (named address book entry) | ✗ (keypool) | ✗ (keypool) | ✓ SignKey (P2PKH only) | ✓ mintkey (P2PKH) |
| P2PKH-kernel-P2PK-input combining | ✗ | ✗ | ✓ | ✗ |
| Stake cache (active) | ✗ | ✗ | ✓ | ✗ |
| `nStakeMinAge` (30 days) | ✗ | ✗ | ✗ | ✓ |
| Dev fund donation output | ✓ | ✗ | ✗ | ✗ |
| Block signature (vchBlockSig) | ✓ | ✓ | ✓ | ✓ |
| P2PK reward output (legacy) | ✓ | ✓ | ✗ (upgraded to P2PKH) | ✓ |
| `LOCK2(cs_main, cs_wallet)` order | cs_main first | cs_main first | cs_main first | cs_main first |
| `txNew.nTime -= n` after kernel | ✓ | ✓ | ✓ | ✓ |
| Kernel `n > 0` produces rejected block | ✓ (latent) | ✓ (latent) | ✓ (latent) | ✗ (valid) |

---

## 16. Migration Timeline

| Date | Commit | Description |
|------|--------|-------------|
| 2017 | `2fdd12b2ea` (Blackcoin Lore) | Initial fork from Peercoin. Inherits `pindexBestHeader` guard, dynamic `nSearchInterval`, `nMaxStakeSearchInterval = 60`, no MTP check in CreateCoinStake. Dev fund donation present. |
| 2018-2022 | (Bitcoin Core merges) | Many upstream changes. Staking code is gradually refactored. |
| 2023 (Jan) | `a10767cc47` (lateminer) | `nSearchInterval` changed from `nSearchTime - nLastCoinStakeSearchTime` to hardcoded `1`. Title: "Use txCoinStake.nTime when staking". |
| 2026 (Mar) | `5d68573155` (BlackcoinDev) | "staking: fix various issues, add new features". Staking code moved to `src/wallet/staking.cpp`. Stake cache refactored. |
| 2026 (Jul) | `57d605d415` (BlackcoinDev) | "pos: modernize staking protocol". Adds SignKey, OP_RETURN carrier, MTP check, nTimeSmart check, P2PK→P2PKH reward upgrade, P2PK sweep via `combineP2PK`, `MsUntilNextWindow`, timer guard with `m_last_coin_stake_search_tip`. Header spam filter address key changed from `GetAddrLocal` to `pfrom.addr`. |
| 2026 (Jul) | `beaaa0601f` (BlackcoinDev) | "net: fix header-spam-filter from upstream qtum, add additional logging". |
| 2026 (Jul) | `119b138c74` | "wallet: Log CWalletTx's internal state in AddToWallet". Fixes log to show final state, not input state. |

---

## 17. The `pindexBestHeader` Race Guard — Detailed Analysis

### What 132 had

```cpp
// src/wallet/wallet.cpp:726-727
for (unsigned int n=0; n<min(nSearchInterval,(int64_t)nMaxStakeSearchInterval)
                     && !fKernelFound
                     && pindexPrev == pindexBestHeader; n++)
```

`pindexBestHeader` was a global in 132 (`src/main.cpp:3089`):

```cpp
CBlockIndex *pindexBestHeader = NULL;
```

Updated in two places:
- `main.cpp:3089`: when a new header is received with more work
- `main.cpp:3107`: during InitializeChainTip

`GetPastTimeLimit()` (used in 132's SignBlock) returns the MTP of `pindexBestHeader`.

### What 132 protected against

The 132 check was a **defensive optimization**, not a correctness fix. With
proper lock ordering, the kernel search sees a consistent chain state. The
extra check prevented the staking thread from building a coinstake on a chain
that was about to be reorganized.

Scenario:
1. Wallet is at chain tip T (work W)
2. Wallet starts kernel search with `pindexPrev = T`
3. Peer sends new block B with work W+1 on a different branch
4. `pindexBestHeader` updates to B's tip (without `cs_main` being held)
5. Active chain tip is still T (validation pending)
6. Wallet finds kernel based on `pindexPrev = T`
7. Builds coinstake with `hashPrevBlock = T`
8. B finishes validation, becomes new active tip
9. Wallet's coinstake is now on a stale fork

In 132, step 7 would be aborted because `pindexPrev (T) != pindexBestHeader (B's tip)`.
In 262+, the check is gone, so the staking thread wastes CPU on step 7.

### Why the check was lost

When Blackcoin 262 merged more recent Bitcoin Core upstream:
- `pindexBestHeader` global was removed (replaced by `m_chainman.m_best_header`)
- The staking loop's check was not migrated to use the new field
- The check is absent in 262, 272, 284, and Peercoin

### Modern equivalent

A check that uses `m_chainman.m_best_header` would be:

```cpp
for (unsigned int n = 0;
     n < std::min(nSearchInterval, (int64_t)nMaxStakeSearchInterval) && !fKernelFound
     && pindexPrev == m_chainman.m_best_header;  // modern equivalent of pindexBestHeader
     n++)
```

This would restore 132's defensive check using modern APIs. Impact: minor CPU
savings on stale-fork scenarios. Not a security fix.

### Practical impact today

In 284, the worst case is:
1. Staking thread builds a coinstake based on `pindexPrev` (old tip)
2. Chain advances to a new tip
3. Block validation rejects the coinstake (it's on a stale fork)
4. PoSMiner sees the rejection, sleeps until next window, tries again

User impact: none. Network impact: minor wasted bandwidth/CPU on the local
node. Not a consensus issue.

---

## 18. References

- `src/wallet/staking.cpp` — Blackcoin 262/272/284 CreateCoinStake
- `src/wallet/wallet.cpp` — Blackcoin 132 CreateCoinStake (also Peercoin)
- `src/node/miner.cpp` — PoSMiner (all Blackcoin versions)
- `src/main.cpp` — Blackcoin 132 SignBlock, ThreadStakeMiner
- `src/miner.cpp` — Blackcoin 132 miner
- `src/pos.cpp` — CheckCoinStakeTimestamp, CheckKernel
- `src/validation.cpp` — block validation, AddToWallet logging
- `src/net_processing.cpp` — header spam filter (284)
- `src/node/miner.cpp` — MsUntilNextWindow (284)

### Key commit hashes

- `2fdd12b2ea` (2017) — Blackcoin Lore (initial fork)
- `a10767cc47` (2023) — nSearchInterval = 1 change
- `5d68573155` (2026) — staking refactor
- `57d605d415` (2026) — modernize staking protocol
- `beaaa0601f` (2026) — header spam filter fix
- `119b138c74` (2026) — AddToWallet log fix
