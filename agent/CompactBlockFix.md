# BIP152 Compact Block Prefill Fix for PoS

## Problem

BIP152 compact block relay (`blockencodings.cpp`) prefills only `vtx[0]` — the coinbase for PoW blocks, or the empty marker for PoS blocks. The coinstake at `vtx[1]` is assigned a short ID, like any regular transaction.

The coinstake is **never in any peer's mempool** — it is freshly created by the staker in `CreateCoinStake` and submitted directly via `ProcessBlockFound`. This means the short ID always misses during `PartiallyDownloadedBlock::InitData`, forcing a `getblocktxn` → `blocktxn` round-trip for every PoS compact block.

Before the fix, compact blocks gave **zero round-trip benefit** for minimal PoS blocks (marker + coinstake only, no regular txs). The sequence was:

```
cmpctblock received → coinstake short ID misses → send getblocktxn (34 bytes)
→ receive blocktxn (316 bytes) → FillBlock reconstructs → block connects
```

Log evidence (before fix):
```
Successfully reconstructed block ... with 1 txn prefilled, 0 txn from mempool, 1 txn requested
```

## Fix

**File:** `src/blockencodings.cpp:20-43`

The constructor `CBlockHeaderAndShortTxIDs` now checks `block.IsProofOfStake()`. For PoS blocks, it prefills both `vtx[0]` (marker) and `vtx[1]` (coinstake), matching how Bitcoin Core always prefills the coinbase for PoW. PoW blocks are unchanged.

```cpp
if (block.IsProofOfStake()) {
    prefilledtxn = {{0, block.vtx[0]}, {0, block.vtx[1]}};
    shorttxids.resize(block.vtx.size() - 2);
    for (size_t i = 2; i < block.vtx.size(); i++) {
        shorttxids[i - 2] = GetShortID(block.vtx[i]->GetWitnessHash());
    }
} else {
    prefilledtxn = {{0, block.vtx[0]}};
    shorttxids.resize(block.vtx.size() - 1);
    for (size_t i = 1; i < block.vtx.size(); i++) {
        shorttxids[i - 1] = GetShortID(block.vtx[i]->GetWitnessHash());
    }
}
```

### Differential index encoding

The `PrefilledTransaction.index` field uses **differential encoding** (BIP152 spec): each entry's index is the number of shorttxid slots since the last prefilled entry. Since the marker (block index 0) and coinstake (block index 1) are adjacent with zero shorttxids between them, the second entry carries `index=0`, not `index=1`.

In `InitData`, `lastprefilledindex` starts at -1:
- Entry 0: `lastprefilledindex = -1 + 0 + 1 = 0` → `txn_available[0]` = marker
- Entry 1: `lastprefilledindex = 0 + 0 + 1 = 1` → `txn_available[1]` = coinstake

Shorttxids then map to positions 2, 3, 4, ... via `index_offset` that skips filled slots.

### Verification (after fix)

Log evidence:
```
Successfully reconstructed block ... with 2 txn prefilled, 0 txn from mempool, 0 txn requested
```

Zero round-trips. The `cmpctblock` (599 bytes) arrives, reconstructs immediately, block connects in <1ms. No `getblocktxn`/`blocktxn` exchange needed.

## Network Impact

### Orphan reduction

Before the fix, every PoS compact block needed a round-trip (50-500ms per hop) during which the receiving node was still on the old tip and could find a competing block. Eliminating the round-trip on every hop reduces total propagation latency, directly shrinking the window where competing blocks emerge.

For minimal PoS blocks (the majority — most blocks have 0 regular txs), compact blocks previously gave **zero benefit** over regular `inv → getdata → block` relay (same round-trip count). Now they are single-message, same as PoW coinbase blocks.

### BIP152 peer limits (context)

- **High-bandwidth (to):** max 3 outbound peers receive unsolicited `cmpctblock` announcements (`lNodesAnnouncingHeaderAndIDs` capped at 3, `net_processing.cpp:1561`). Comment: "As per BIP152, we only get 3 of our peers to announce blocks using compact encodings."
- **High-bandwidth (from):** any number of inbound peers can request HB mode from us; no cap.
- **In-flight per block:** max 3 (`MAX_CMPCTBLOCKS_INFLIGHT_PER_BLOCK`, `net_processing.h:36`).
- **Outbound full-relay limit:** 16 (`MAX_OUTBOUND_FULL_RELAY_CONNECTIONS`, `net.h:67` — raised from 8 in Bitcoin Core v22.0).
- The code protects against evicting the last outbound HB peer in favor of an inbound one (`net_processing.cpp:1547-1557`).

With 3 HB peers per node, a block reaches N nodes in ~log₃(N) hops. For ~1000 staking nodes, ~7 hops. Each hop now saves one round-trip for PoS blocks.

### `NewPoWValidBlock` fires for both PoW and PoS

Despite the name (legacy Bitcoin Core), `NewPoWValidBlock` fires for any block building on the active tip (`validation.cpp:4591`):
```cpp
if (!IsInitialBlockDownload() && ActiveTip() == pindex->pprev && m_options.signals) {
    m_options.signals->NewPoWValidBlock(pindex, pblock);
}
```
On testnet (which has both PoW and PoS blocks), it fires for both. The fast-announcement path (`net_processing.cpp:2414`) constructs a compact block and pushes it to all HB peers that have the parent header but not the new block.

### Wire compatibility

`prefilledtxn` is a generic vector on the wire. `BlockTxCount()` is unchanged (`shorttxids.size() + prefilledtxn.size()`). Old nodes receiving a compact block with 2 prefilled entries reconstruct correctly — they just weren't sending them this way. No fork risk.

### Construction paths covered

All three compact block construction paths use the same constructor:
1. `net_processing.cpp:2416` — `NewPoWValidBlock` fast announcement (block builds on active tip)
2. `net_processing.cpp:2802` — `getdata` response with `MSG_CMPCT_BLOCK` (low-bandwidth)
3. `net_processing.cpp:6279` — `SendMessages` header announcement fallback

All benefit from the fix automatically.

## Relevant Files

- `src/blockencodings.cpp:20-43` — the fix (constructor prefill logic)
- `src/blockencodings.cpp:63-175` — `InitData` (receiver-side reconstruction, handles 2 prefilled correctly)
- `src/blockencodings.h:74-82` — `PrefilledTransaction` struct with differential `index`
- `src/net_processing.cpp:2414-2463` — `NewPoWValidBlock` fast announcement
- `src/net_processing.cpp:1525-1578` — HB peer selection (max 3, LRU eviction)
- `src/net_processing.h:36` — `MAX_CMPCTBLOCKS_INFLIGHT_PER_BLOCK = 3`
- `src/net.h:67` — `MAX_OUTBOUND_FULL_RELAY_CONNECTIONS = 16`
- `src/primitives/block.h:132-135` — `IsProofOfStake()` (checks `vtx[1]->IsCoinStake()`)