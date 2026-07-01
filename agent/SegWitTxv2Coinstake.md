# v2 Coinstake — TxID Collision Analysis

**Status:** COMPLETED. (The collision is fixed natively via Option E: embedding the masked timestamp in the OP_RETURN carrier).
## The Root Cause

Blackcoin v2 transactions drop `nTime` from serialization:

```cpp
// transaction.h
if (tx.version < 2)
    s << tx.nTime;   // v1: serialized (4 bytes)
else
    tx.nTime = 0;     // v2: stripped
```

The same check exists in **both** sighash paths:
- **Legacy sighash** (`interpreter.cpp:1333-1335`): `CTransactionSignatureSerializer` only serializes `nTime` if `version < 2`
- **BIP143 (segwit)** (`interpreter.cpp:1609-1612`): same — only includes `nTime` if `version < 2`

Since `nTime` is excluded from the sighash, the message being signed is identical every time → RFC6979 deterministic signatures produce the **same signature** → `scriptSig` is the same → the serialized non-witness bytes are identical → **same txid**.

This affects **all** single-input v2 coinstakes regardless of wallet or address type.

```
v2 serialization:  [version=2][vin: {prevout, scriptSig=(same sig), sequence}][vout][nLockTime=0]
                      ↓                         ↓                               ↓           ↓
                   always 2            identical each attempt          identical each attempt   0
```

## When TxID Collides

Same UTXO staked twice at different times in a v2 coinstake:

| Field | Attempt 1 (T1) | Attempt 2 (T2) | Same? |
|---|---|---|---|
| `version` | 2 | 2 | Yes |
| `vin[0].prevout` | UTXO | UTXO | Yes |
| `vin[0].scriptSig` | sig | sig (RFC6979, same message) | **Yes** |
| `vin[0].nSequence` | same | same | Yes |
| `vout[].scriptPubKey` | same destination | same destination | Yes |
| `vout[].nValue` | same reward (same height) | same reward (same height) | Yes |
| `nLockTime` | 0 | 0 | Yes |
| **Non-witness bytes** | — | — | **Identical** |
| **TxID** | X | **X (same!)** | **Yes ✗** |
| `nTime` (in memory) | T1 | T2 | No (but stripped from serialization & sighash) |
| Signature | commits to T1 | commits to T2 | No (sighash excludes nTime for v2 → same message) |

**This is a universal property of v2 coinstakes** — not specific to segwit or any wallet type.

## Historical Misconception

Previous versions of this doc incorrectly claimed legacy (P2PKH) coinstakes avoided the collision because the signature goes in `scriptSig` and "varies per attempt." This is wrong:
1. For v2, **both** legacy and BIP143 sighash exclude `nTime`
2. Same transaction bytes → same sighash → RFC6979 → same signature → same `scriptSig` → same txid
3. Wallet type (legacy BDB vs descriptor SQLite) is irrelevant
4. `SignTransaction` zeroing `nTime` at `sign.cpp:823` is redundant for v2 (the legacy `CTransactionSignatureSerializer` also skips it at `interpreter.cpp:1333`)

## When This Actually Happens

1. Local node stakes block at height H with coinstake txid=X
2. Block gets orphaned (competitor's heavier chain wins)
3. UTXO becomes spendable again
4. Wallet retries same UTXO at next window
5. Creates new coinstake with **same txid=X** (all v2 coinstakes from same UTXO have same txid)
6. Two different blocks contain transactions with the same txid

## Qtum Comparison

Qtum has the same deterministic-txid issue — no `nTime` field in transactions, v2 default. But Qtum's consensus **doesn't rely on txid uniqueness** for staking.

Qtum's block header includes `prevoutStake` (`COutPoint`), which identifies which UTXO is being staked. Combined with `block.nTime`, each stake has a unique `(prevoutStake, nTime)` pair at the block header level. Qtum's `setStakeSeen` (`validation.cpp:136, 6305`) is a `std::set<std::pair<COutPoint, unsigned int>>` that rejects duplicate `(prevoutStake, block.nTime)` before accepting a header.

| | Blackcoin (current) | Qtum |
|---|---|---|
| Transaction `nTime` | Only in v1; stripped in v2 | Nonexistent |
| TxID deterministic for v2 coinstakes? | **Yes — always** | **Yes — same** |
| `prevoutStake` in block header | No | **Yes** |
| `setStakeSeen` anti-dupe | No | **Yes** — `(prevoutStake, block.nTime)` |
| Problem exists? | **Yes** (no mitigation) | **No** (consensus-level uniqueness via header) |

## Viable Solutions

### Option A: Qtum's `setStakeSeen` (no fork, ~20 lines)

Add a set of `(COutPoint, uint32_t)` pairs recording every accepted coinstake kernel. Check in `AcceptBlockHeader`. Qtum-proven, no serialization or consensus changes. Doesn't prevent txid collision itself — prevents duplicate acceptance at runtime.

### Option B: `prevoutStake` in block header (soft fork)

Include the kernel prevout in the block header (like Qtum). Commits the kernel input to the block hash. Enables delegation. Larger change across serialization, validation, mining.

### Option C: `nTime` back in v2 serialization (hard fork — DO NOT)

Re-adding `nTime` to v2 serialization changes every v2 txid. Permanent chain split. Not viable.

### Option D: Do nothing

| Concern | Risk | Why |
|---|---|---|
| Block relay | **Low** | Blocks identified by hash, not txid |
| UTXO set | **Low** | Orphan disconnect removes; reconnect adds |
| Wallet tracking | **Medium** | Same txid from orphan + new block could confuse state |
| CoinStatsIndex | **Low** | MuHash insert/remove cancel correctly |
| Peer mempool | **Low** | Coinstakes bypass mempool |

Requires: v2 coinstake (default) + same UTXO + same height + orphan + retry. Edge case.

### Option E: OP_RETURN carrier with per-window nonce (no fork, ~10 lines) — IMPLEMENTED

Embed the block timestamp (the masked 16-second boundary) as a **3rd push** in `vout[1]`:

```
vout[1]: OP_RETURN <33-byte-pubkey> <4-byte-timestamp>
```

`CheckBlockSignature` (validation.cpp:3876-3893) reads exactly **2** `GetOp` calls — `OP_RETURN` + `pubkey`. The 3rd push is **ignored by the verifier** but **included in the serialized transaction** and therefore in the txid.

Each retry after an orphan lands in a different 16-second window → different timestamp → different `vout[1]` → **different txid**. The signature is unchanged (sighash still excludes `nTime` for v2), but the txid varies because `vout[1]` varies.

**No consensus change**: the OP_RETURN carrier path is already accepted by `CheckBlockSignature`. The 3rd push is never read. This is **provably safe under current rules**. Proven on-chain: block `acd7b37c...` uses `OP_RETURN <pubkey> "STAND FOR PEACE!"` — a 3rd push accepted by consensus today.

### Is the timestamp actually needed?

**No — the timestamp is not needed for consensus.** It is purely a defensive measure against a narrow edge case (orphan + retry from the same UTXO at the same height). The risks from the collision without the timestamp:

| Concern | Risk | Why |
|---|---|---|
| Block relay | **Low** | Blocks identified by hash, not txid |
| UTXO set | **Low** | Orphan disconnect removes; reconnect adds |
| Wallet tracking | **Medium** | Same txid from orphan + new block could confuse state |
| CoinStatsIndex | **Low** | MuHash insert/remove cancel correctly |
| Peer mempool | **Low** | Coinstakes bypass mempool |

Without the timestamp, `OP_RETURN <pubkey>` alone is sufficient for block signing. The timestamp costs 5 bytes (1 byte push opcode + 4 bytes LE timestamp) per coinstake to avoid a rare wallet-tracking edge case. It could be removed with no consensus impact.

### Regtest Verification (June 25, 2026)

Single example (legacy BDB wallet, height 664):
```
Block hash: ed0e6b9115b14c6a6e28becc405eeb968b79ab19fb0f6ba831f26f19663ea7b2

vout[1]: OP_RETURN 03205d5a9d96b305fce60daa2454b4d13e9e8bb2b1c49bcaffa8650188485a9793 1782409456
  - Push 1: OP_RETURN (ignored by CheckBlockSignature)
  - Push 2: 03205d5a9d96b305fce60daa2454b4d13e9e8bb2b1c49bcaffa8650188485a9793 (pubkey, verified)
  - Push 3: 1782409456 (timestamp, ignored by CheckBlockSignature but varies txid)

vout[2]: 1251.31 BLK (P2PK)
vout[3]: 1251.32 BLK (P2PK)
Block sig: 30440220117ab20c938059418b21fb72bb89e6f83a4376a3a9da040352343342d7ec5db40220683e6157c1c161a9708f61421a030662d145218b1060e137782df842b3241ce0
```

Summary of all 4 regtest stakes:

| Height | Wallet type | Kernel | Timestamp | Txid varies? | Accepted? |
|---|---|---|---|---|---|
| 664 | Legacy BDB | P2PK | 1782409456 | ✅ Different per window | ✅ |
| 676 | Legacy BDB | P2PK | 1782409648 | ✅ | ✅ |
| 687 | Descriptor SQLite | P2PK (legacy addr) | 1782409824 | ✅ | ✅ |
| 705 | Descriptor SQLite | P2WPKH (BECH32) | 1782410112 | ✅ | ✅ |

All demonstrate: timestamp varies per 16-second window, changing vout[1] bytes → different txid. Block signature unchanged (ECDSA via carrier pubkey). No consensus change.

**Pros:**
- No fork — uses existing consensus rules
- Breaks txid collision at the source (txid varies per window)
- Enables P2PKH/P2WPKH/P2TR reward outputs as a side effect
- ~10 lines in `CreateCoinStake`
- Timestamp already in memory (`txNew.nTime`)

**Cons:**
- 5 bytes per coinstake (1 byte push opcode + 4 bytes LE timestamp)
- One extra dust UTXO per staked block (the OP_RETURN)

**Comparison with Option A (`setStakeSeen`)**: Option A prevents *acceptance* of duplicates at runtime but not the collision itself. Option E prevents the collision at the **txid level**. Both can coexist; Option E is cleaner.

---

## Key Files

- `src/primitives/transaction.h:233-236, 277-278` — nTime stripped for v2
- `src/script/interpreter.cpp:1333-1335` — legacy sighash strips nTime for v2
- `src/script/interpreter.cpp:1609-1612` — BIP143 sighash strips nTime for v2
- `src/script/sign.cpp:823-824` — `SignTransaction` zeroes nTime (redundant for v2)
- `src/wallet/staking.cpp` — carrier mechanism in `CreateCoinStake`
- `src/validation.cpp:3876-3893` — `CheckBlockSignature` OP_RETURN path
- `../qtum/src/validation.cpp:136, 6305` — Qtum's `setStakeSeen`
- `../qtum/src/primitives/block.h:25-42` — Qtum's `prevoutStake` in header
- `agent/P2PKMigration.md` — combining-final description and on-chain audit
