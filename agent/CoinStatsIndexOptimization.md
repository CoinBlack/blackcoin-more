# CoinStatsIndex — Performance Optimization

## History of Changes

The CoinStatsIndex bug was a chain of interconnected issues stemming from one root cause: **v2 transactions don't serialize `nTime`**, but the `Coin` object and MuHash depend on it.

### Commit 1: `5bb1305` — Add `fCoinStake` and `nTime` to MuHash

**What**: Changed `TxOutSer()` in `kernel/coinstats.cpp` to encode `fCoinStake` and `nTime` into the MuHash serialization.

**Why**: Without these fields, the MuHash couldn't distinguish coinstake outputs from regular outputs, and couldn't track per-UTXO timestamps. This was needed for accurate UTXO set hashing.

**Consequence**: MuHash now depends on correct `nTime` values. But `coin.nTime` was always `0` for v2 txs (deserialization artifact), so the MuHash was computed from wrong data. This didn't cause a crash initially — it just silently produced incorrect hashes.

### Commit 2: `8430495` — Fix nTime for new-output paths in CoinStatsIndex

**What**: In `CustomAppend` and `ReverseBlock`, fixed the `Coin` construction for **new outputs** (from `tx->vout`) to use `block.nTime` instead of `tx.nTime` for v2 coinstakes.

**Why**: The new outputs were being added to MuHash with correct `nTime` (from our fix: `block.nTime`), but the undo-data paths still read `nTime = 0` from disk. This created a mismatch: insert used `block.nTime`, remove used `0` → MuHash drifted → **assertion crash on reorg** when `ReverseBlock` called `Finalize()` and compared against the DB value.

**Consequence**: The assertion started firing every time your node staked a block and it got orphaned.

### Applied to working tree: `AddCoins` root-cause fix + CoinStatsIndex fallback

**What**: Three changes applied to the v28-CORE working tree:

1. **`AddCoins` fix** (coins.cpp, coins.h): Added `nBlockTime` parameter. For v2 txs, stores `nTime = block.nTime` instead of `tx.nTime = 0` in the `Coin`. This fixes: UTXO cache, undo data, `tx_verify.cpp:191` temporal check — everything downstream.

2. **CoinStatsIndex fallback** (coinstatsindex.cpp): In both `CustomAppend` and `ReverseBlock`, when reading coins from undo data, if `nTime == 0 && coin.nHeight > 0`, reconstruct `nTime` from the creation block via `pindex->GetAncestor(coin.nHeight)->nTime`. This handles old undo data written before the fix without requiring a full `-reindex`.

3. **Validation.cpp plumbing**: `UpdateCoins` and `ConnectBlock` now pass `block.nTime` through to `AddCoins`.

**Why**: The earlier fix (commit 2) only fixed half the problem — new outputs but not undo data. This fixes the source (`AddCoins`), so every consumer gets correct `nTime` automatically.

### Related: SegWitTxv2Coinstake.md

**What**: Document analyzing a txid collision edge case for v2 segwit coinstakes on reorg.

**Why**: The same root cause (v2 drops `nTime` from serialization) means a segwit coinstake's non-witness bytes are identical across retries with the same UTXO and outputs. Not a fork, but a correctness concern. Qtum protects against this with `setStakeSeen`. Documented for future reference.

**Fix found**: the collision can be solved with no fork by embedding the in-memory `txNew.nTime` (masked 16s boundary) as a 3rd push in the `vout[1]` OP_RETURN carrier. `CheckBlockSignature` reads only 2 `GetOp`s and ignores the 3rd, so the timestamp varies the txid per window without affecting consensus. See `agent/SegWitTxv2Coinstake.md` §Option E and `agent/P2PKMigration.md` §6.

---

## Current State

The CoinStatsIndex assertion crash is fixed. The index is rebuilding. But it's **very slow** — same slowness as Bitcoin Core and Qtum. Here's why:


