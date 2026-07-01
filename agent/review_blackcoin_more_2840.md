# Blackcoin More v28.4.0 — Staged Changes Review

**Date:** July 1, 2026
**Scope:** 49 files, ~672 insertions, ~481 deletions
**Branch:** v28-SEGWIT

---

## 1. Change Summary

This changeset implements several interconnected Blackcoin protocol upgrades for the v28.4.0 release.

### 1.1 OP_RETURN SignKey Carrier (Coinstake Restructure)

The coinstake transaction structure changes from:

```
vout[0]: empty (marker)
vout[1]: reward (P2PK or P2PKH)
```

to:

```
vout[0]: empty (marker)
vout[1]: OP_RETURN <pubkey> <timestamp>  (non-spendable carrier)
vout[2]: reward (native kernel type: P2PK, P2PKH, P2WPKH, P2TR)
vout[3]: split reward (if nCredit ≥ 1000 BLK)
vout[4]: devfund (if enabled)
```

- The `bMinterKey` flag and P2PK intermediate output are removed.
- All kernel types use the same SignKey pubkey for the carrier.
- `CheckBlockSignature` already accepts the OP_RETURN carrier path — **no fork required**.
- P2PK kernels auto-upgrade reward output to P2PKH.
- The embedded timestamp (masked 16s boundary) in the OP_RETURN carrier breaks the v2 txid collision edge case (orphan + retry from same UTXO).

### 1.2 SegWit Burial

SegWit is moved from a version-bits deployment to a buried deployment (`DEPLOYMENT_SEGWIT` becomes `BuriedDeployment`).

- `fIncludeWitness` gate removed.
- `OLD_VERSION` / pre-SegWit peer logic removed entirely.
- SegWit script verification flags promoted to `MANDATORY_SCRIPT_VERIFY_FLAGS`.
- SegWit is now always active on all networks.

### 1.3 Stake Cache (`-stakecache`)

New optional per-wallet caching layer for `CheckKernel` results.

- Pre-populates cache in `CreateCoinStake` and `checkkernel` RPC.
- Clears cache when `cache.size() > setCoins.size() + 100`.
- Reduces `GetCoin` + `GetAncestor` disk I/O for repeated kernel checks.

### 1.4 Wake-on-Block Staker Timer

Replaces the naive `UninterruptibleSleep` polling loop with a `condition_variable`-based wake mechanism.

- `updatedBlockTip()` becomes a pure wake signal — sets `m_new_block_arrived = true`, notifies `cv_new_block`. No timing math.
- `MsUntilNextWindow()` computes milliseconds until next 16s boundary, strictly advancing past MTP.
- Per-wallet `m_last_coin_stake_search_time` replaces the global `static nLastCoinStakeSearchTime`.
- `pos_timio` preserved as CPU floor: `max(MsUntilNextWindow(...), pos_timio)`.
- `m_safety_bump_sleep_ms` removed from wallet.

### 1.5 v2 Transaction `nTime` Recovery

Blackcoin v2 transactions don't serialize `nTime` (it's always 0 after deserialization). Recovery logic added:

- `AddToWallet` and `LoadToWallet`: reconstruct `nTime` from `nTimeSmart` / block header.
- `AddCoins`: new `nBlockTime` parameter. For v2 txs, stores `block.nTime` instead of `tx.nTime = 0` in the `Coin`.
- `UpdateCoins` and `RollforwardBlock`: pass `nBlockTime` through to `AddCoins`.
- `CoinStatsIndex`: fallback reconstructs `nTime` from block index for old undo data (avoids mandatory reindex for the nTime fix alone).

### 1.6 Default Address Type

Changed from `LEGACY` (P2PKH) to `BECH32` (P2WPKH) for new addresses.

### 1.7 P2TR Signing Fix

`SCRIPT_VERIFY_TAPROOT` was missing from `STANDARD_SCRIPT_VERIFY_FLAGS` in `policy.h`.

- Without it, P2TR signing silently failed: `VerifyWitnessProgram` returned `set_success` without checking the witness, causing `::SignTransaction` to return `true` on the wrong `ScriptPubKeyMan`.
- Fix: added `SCRIPT_VERIFY_TAPROOT` to `STANDARD_SCRIPT_VERIFY_FLAGS` (standard-only, not mandatory — peers not banned for invalid P2TR on networks where Taproot is not yet consensus).
- Also added `SCRIPT_VERIFY_WITNESS` and `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` to `MANDATORY_SCRIPT_VERIFY_FLAGS` (SegWit merge gap).

### 1.8 Input Combining Fixes

**COutPoint fix (June 29):**
- Combining guard compared only `pcoin.first->GetHash()` (txid), rejecting all same-tx sibling UTXOs.
- Fixed to compare full `COutPoint(pcoin.first->GetHash(), pcoin.second)`.
- Affects all kernel types. Real-world impact: `optimizeutxoset` creating 42 identical P2WPKH outputs could only combine 1 with the kernel.

**`nTimeSmart` fix (June 26):**
- Combining guard used `pcoin.first->tx->nTime` (always 0 for v2 txs), making the timing guard a no-op.
- Fixed to use `pcoin.first->nTimeSmart` (block time for confirmed txs).

### 1.9 Other Changes

- **`ProcessBlockFound`**: Removed stale-block check (`hashPrevBlock != activeTip`). The counterattack-window problem is now handled by proper fork submission.
- **`RelaxNetWorkMask`**: Dev toggle switches IPv4 from /16 to /32 and IPv6 from /32 to /128. Defaults to `true` (intentional testing choice — will be set to `false` before production release).
- **`ComputeTimeSmart`**: Tolerance reduced from 5 minutes to 16 seconds (`latestNow + 16`).
- **`sendtoaddress`**: `fee_rate` parameter removed; subsequent parameter indexes shifted.
- **`burn` / `burnwallet` RPCs**: Now use `SendMoney` instead of removed `SendMoneyToScript`.
- **`optimizeutxoset`**: Switched from balance-based to coin-selection-based input gathering.
- **`GetStakeWeight`**: Now acquires `cs_wallet` lock.
- **`DelAddressBook`**: Gains `force` parameter to protect SIGNKEY addresses from deletion.
- **`g_txindex->FindTx` calls**: Replaced with cached `CWalletTx` data in staking path.
- **CoinStats serialization**: Format change — `(nHeight << 1) + fCoinBase` → `(nHeight << 2) + fCoinBase + fCoinStake` + `VARINT(nTime)`.

---

## 2. Issues Found

### 2.1 ~~MEDIUM~~ RESOLVED (intentional): `RelaxNetWorkMask` defaults to `true` — testing choice

**File:** `src/netgroup.cpp:15`

```cpp
static bool g_relax_network_mask = true; // blackcoin: using true for testing
```

With `true`, every IPv4 address becomes its own `/32` group and every IPv6 address becomes `/128`. This negates `GetGroup()` anti-sybil/eclipse protections.

**Status:** Intentional choice for ongoing testing. Will be set to `false` when testing is complete and the release is finalized. Not a bug — tracked as a deliberate decision.

### 2.2 ~~MEDIUM~~ VERIFIED CORRECT: `sendtoaddress` parameter index shift

**File:** `src/wallet/rpc/spend.cpp`

Blackcoin removed 4 parameters from Bitcoin Core's `sendtoaddress`: `replaceable`, `conf_target`, `estimate_mode`, and `fee_rate`. The new parameter order is:

| Index | Parameter | `request.params[N]` access | Correct? |
|---|---|---|---|
| 0 | `address` | `params[0]` | ✅ |
| 1 | `amount` | `params[1]` | ✅ |
| 2 | `comment` | `params[2]` | ✅ |
| 3 | `comment_to` | `params[3]` | ✅ |
| 4 | `subtractfeefromamount` | `params[4]` | ✅ |
| 5 | `avoid_reuse` | `params[5]` | ✅ |
| 6 | `verbose` | `params[6]` | ✅ |

All `request.params[N]` accesses match the new RPC declaration order. No references to removed parameters (`replaceable`, `conf_target`, `estimate_mode`, `fee_rate`, `SetFeeEstimateMode`, `m_signal_bip125`) remain. RPC help examples are also correct — they only reference existing parameters.

### 2.3 ~~MEDIUM~~ FIXED: `burn` RPC balance info leak

**File:** `src/wallet/rpc/spend.cpp`

`GetBalance` was called before `EnsureWalletIsUnlocked`, allowing an attacker with a locked wallet to distinguish "insufficient funds" from "wallet locked" error responses, potentially leaking balance information.

**Fix applied:** Moved `EnsureWalletIsUnlocked(*pwallet)` before the `GetBalance` call. Now a locked wallet returns `RPC_WALLET_UNLOCK_NEEDED` before any balance information is revealed. `burnwallet` RPC was already correct (unlock before balance check).

### 2.4 ~~LOW~~ ACKNOWLEDGED: CoinStats serialization format is a breaking change

**File:** `src/kernel/coinstats.cpp`

The serialization format changed from:
```cpp
ss << static_cast<uint32_t>((coin.nHeight << 1) + coin.fCoinBase);
```
to:
```cpp
ss << static_cast<uint32_t>((coin.nHeight << 2) + (coin.fCoinBase ? 1u : 0u) + (coin.fCoinStake ? 2u : 0u));
ss << VARINT(coin.nTime);
```

Existing CoinStats indexes are unreadable after this change — a reindex is required. However, no applications currently use the CoinStatsIndex feature, so the impact is negligible. The `CustomAppend` fallback for `nTime == 0` old undo data handles the undo path for those who do have an existing index.

### 2.5 LOW: Fuzz test has commented-out variable with potential dangling references

**File:** `src/wallet/test/fuzz/fees.cpp:49`

```cpp
// const auto tx_bytes{fuzzed_data_provider.ConsumeIntegral<unsigned int>()};
```

If `tx_bytes` was used below (even in commented-out code), this creates confusion. Should be fully removed or properly integrated.

### 2.6 ~~LOW~~ ACKNOWLEDGED: `GetStakeWeight` recursive lock on `cs_wallet`

**File:** `src/wallet/staking.cpp`

`GetStakeWeight` now acquires `LOCK(wallet.cs_wallet)`. It's called from `CreateCoinStake`, which already holds `cs_wallet` via `LOCK2(cs_main, wallet.cs_wallet)`. Since `cs_wallet` is a `RecursiveMutex`, this is safe. The recursive acquisition is intentional — `GetStakeWeight` is also called from non-locked contexts (e.g., `getstakinginfo` RPC), so it needs its own lock.

### 2.7 ~~BEHAVIORAL~~ ACKNOWLEDGED: `nTimeSmart` combining guard for unconfirmed transactions

**File:** `src/wallet/staking.cpp:427`

```cpp
if (pcoin.first->nTimeSmart > txNew.nTime)
    continue;
```

This prevents `bad-txns-time-earlier-than-input` consensus violations. The original concern was that `nTimeSmart` could be 0 or unreliable for unconfirmed/mempool transactions.

**Verified safe:** `AvailableCoinsForStaking` (`staking.cpp:105,133`) enforces `min_depth = max(DEFAULT_MIN_DEPTH, nCoinbaseMaturity)` = 500 on mainnet. The depth check at line 133 (`nDepth < min_depth → continue`) filters out all unconfirmed UTXOs before they reach the combining loop. Mempool transactions (depth 0) never pass this filter. Only confirmed UTXOs with depth ≥ 500 reach the guard, where `nTimeSmart` is the reliable block time.

### 2.8 ~~BEHAVIORAL~~ ACKNOWLEDGED: `ComputeTimeSmart` tolerance 5min → 16s

**File:** `src/wallet/wallet.cpp:2961`

The `latestTolerated` window reduced from `latestNow + 300` (Bitcoin) to `latestNow + 16` (Blackcoin). This is **correct as-is** — no change needed.

**Why 16 seconds is correct:**
- **FutureDrift** (consensus rule, `validation.cpp:150`): PoS blocks with `nTime > now + 15` are rejected. This is the hard consensus limit.
- **`ComputeTimeSmart`** (wallet heuristic, `wallet.cpp:2961`): Sets `nTimeSmart` for wallet transaction ordering. The 16-second tolerance aligns with the 16-second PoS timestamp boundary (`nStakeTimestampMask = 0xf`), not the 15-second FutureDrift. It's a wallet-level heuristic, not a consensus rule.
- **Coinstakes bypass this entirely** (`wallet.cpp:2951-2952`): `if (wtx.IsCoinStake()) nTimeSmart = blocktime;` — coinstakes always use the block time directly, never the heuristic.
- **Only affects regular transactions**: The `nTimeSmart` value is used for wallet display ordering and the input combining guard (`staking.cpp:427`). It does not affect consensus validity.
- **Practical impact**: A user with >16s clock skew may see regular transaction timestamps downgraded in wallet display. This is cosmetic — the transactions remain valid.

**Why not use 15 (exact FutureDrift)?** The tolerance is about wallet ordering, not block validity. 16 seconds (one staking window) is the natural alignment for Blackcoin's PoS timing. Using 15 would create an off-by-one with the timestamp mask.

---

## 3. P2WPKH/P2TR Kernel Signature Verification Gap — Deep Dive

### 3.1 The Problem: What Happens Today

`CheckProofOfStake` (`pos.cpp:157`) verifies coinstake input signatures by calling:

```cpp
// pos.cpp:157
if (!VerifySignature(coinPrev, txin.prevout.hash, tx, 0, SCRIPT_VERIFY_NONE))
```

`VerifySignature` (`sign.cpp:718-735`) is a wrapper that does:

```cpp
// sign.cpp:720 — amount is 0!
TransactionSignatureChecker checker(&txTo, nIn, 0, MissingDataBehavior::FAIL);
// sign.cpp:734 — witness is nullptr!
return VerifyScript(txin.scriptSig, txout.scriptPubKey, nullptr, flags, checker);
```

**Three problems in one call:**
1. **`SCRIPT_VERIFY_NONE` (flags = 0)**: No witness program detection, no P2SH, no Taproot
2. **`nullptr` witness**: Real witness data is never passed to `VerifyScript`
3. **`amount = 0`**: BIP143 (SegWit v0) sighash includes the input amount — passing 0 produces wrong sighashes

### 3.2 What Each Kernel Type Does Today

Inside `VerifyScript` (`interpreter.cpp:1973+`), the scriptPubKey is executed as a plain script when `SCRIPT_VERIFY_WITNESS` is not set:

| Kernel | scriptPubKey | What happens with SCRIPT_VERIFY_NONE + nullptr witness | Signature checked? |
|---|---|---|---|
| **P2PK** | `<pubkey> OP_CHECKSIG` | Script executes normally, `OP_CHECKSIG` verifies the ECDSA signature | ✅ **Yes** |
| **P2PKH** | `OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG` | Script executes normally, `OP_CHECKSIG` verifies the ECDSA signature | ✅ **Yes** |
| **P2WPKH** | `OP_0 <20-byte-hash>` | Executes as plain pushes: push empty vector, push 20-byte hash. Stack top is truthy (non-empty) → passes | ❌ **No — bypassed** |
| **P2TR** | `OP_1 <32-byte-key>` | Executes as plain pushes: push 1, push 32-byte key. Stack top is truthy → passes | ❌ **No — bypassed** |

**Security implication:** An attacker who knows a valid kernel UTXO (prevout, amount, creation time) can create a coinstake with an **empty or garbage witness** and it will pass `CheckProofOfStake`. The kernel hash still must be valid (requires the stake modifier + prevout + timestamp), so this isn't a "free block" — but the signature on the coinstake input is never verified, meaning someone could potentially spend a UTXO they don't own in a coinstake if they can compute the kernel hash.

### 3.3 Why P2PK/P2PKH Are Unaffected

P2PK and P2PKH scripts contain `OP_CHECKSIG` as part of the script itself. When the interpreter executes the scriptPubKey, it encounters `OP_CHECKSIG`, which forces it to cryptographically verify the signature against the pubkey. This happens regardless of flags — even `SCRIPT_VERIFY_NONE` executes `OP_CHECKSIG`.

Witness programs (P2WPKH, P2TR) do **not** contain `OP_CHECKSIG` in the scriptPubKey. They are just data pushes (`OP_0 <hash>` or `OP_1 <key>`). The actual signature verification happens in `VerifyWitnessProgram`, which is only called when `SCRIPT_VERIFY_WITNESS` is set. Without that flag, the data pushes execute, the stack top is truthy, and `VerifyScript` returns true without any cryptographic check.

### 3.4 What the Fix Does — Kernel by Kernel

The fix replaces the `VerifySignature` wrapper call with a direct `VerifyScript` call that passes:
- The **actual witness data** (`&txin.scriptWitness`)
- The **actual UTXO amount** (`coinPrev.out.nValue`) — needed for BIP143 sighash
- The **PrecomputedTransactionData** — needed for Taproot Schnorr signatures
- The correct **flags**: `SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT`

| Kernel | What happens with the fix | Will existing coinstakes pass? |
|---|---|---|
| **P2PK** | No change — scriptPubKey is not a witness program, `OP_CHECKSIG` still verifies. Witness is ignored (empty). | ✅ **Yes — identical behavior** |
| **P2PKH** | No change — scriptPubKey is not a witness program, `OP_CHECKSIG` still verifies. Witness is ignored (empty). | ✅ **Yes — identical behavior** |
| **P2WPKH** | `VerifyScript` detects witness program (`OP_0` + 20 bytes), calls `VerifyWitnessProgram`. Constructs implied P2PKH script, verifies witness `[DER-sig, pubkey]` against it using BIP143 sighash (with correct amount). | ✅ **Yes — all 61,196 testnet + 3 mainnet P2WPKH coinstakes have well-formed witnesses** |
| **P2TR** | `VerifyScript` detects witness program (`OP_1` + 32 bytes), calls `VerifyWitnessProgram`. With `SCRIPT_VERIFY_TAPROOT`, proceeds to Schnorr signature verification using `PrecomputedTransactionData`. | ✅ **Yes — regtest P2TR coinstakes have valid Schnorr witnesses** |

### 3.5 Why the Amount Matters (Critical Detail)

The current `VerifySignature` passes `amount = 0` to the checker (`sign.cpp:720`). For P2PK/P2PKH this is fine — legacy sighash (BIP143 pre-SegWit) does **not** include the input amount.

For P2WPKH (BIP143 SegWit v0), the amount **is** part of the sighash (`interpreter.cpp:1676-1679`):
```cpp
// Witness sighashes need the amount.
if (sigversion == SigVersion::WITNESS_V0 && amount < 0) return HandleMissingData(m_mdb);
uint256 sighash = SignatureHash(scriptCode, *txTo, nIn, nHashType, amount, sigversion, this->txdata);
```

If `amount = 0` but the actual UTXO value is e.g. 500 BLK, the sighash would be computed with `amount = 0` instead of `amount = 500 BLK`. The signature in the witness was created with the **correct** amount (by the staking wallet's `SignTransaction`). So the verification would compute a **different** sighash and the signature would **fail**.

**This means:** Simply changing the flags without also fixing the amount would **break P2WPKH staking**. The fix must pass `coinPrev.out.nValue` (the actual UTXO amount) to the checker.

### 3.6 Why PrecomputedTransactionData Matters (for P2TR)

For P2TR (Taproot), Schnorr signature verification (`interpreter.cpp:1707`) requires `PrecomputedTransactionData`:
```cpp
if (!this->txdata) return HandleMissingData(m_mdb);  // txdata is nullptr → returns false
```

The current `VerifySignature` creates the checker **without** `txdata` (the pointer is `nullptr`). For P2TR, this would cause `HandleMissingData(FAIL)` to return `false`, failing verification.

**This means:** The fix must also construct `PrecomputedTransactionData` and pass it to the checker. Without it, P2TR staking would **break**.

### 3.7 The Peercoin Reference — Partially Correct

Peercoin's `CheckProofOfStake` (`peercoin/src/kernel.cpp:686-692`):

```cpp
// peercoin/src/kernel.cpp:689
TransactionSignatureChecker checker(&(*tx), nIn, prevOut.nValue, PrecomputedTransactionData(*tx), MissingDataBehavior(1));

// peercoin/src/kernel.cpp:691
if (!VerifyScript(tx->vin[nIn].scriptSig, prevOut.scriptPubKey,
    &(tx->vin[nIn].scriptWitness), SCRIPT_VERIFY_P2SH, checker, nullptr))
```

Peercoin gets these right:
- `prevOut.nValue` — actual UTXO amount ✅
- `PrecomputedTransactionData(*tx)` — precomputed tx data ✅
- `&(tx->vin[nIn].scriptWitness)` — actual witness data ✅

But Peercoin uses **only `SCRIPT_VERIFY_P2SH`** — missing `SCRIPT_VERIFY_WITNESS`. This means:
- P2PK kernels: ✅ Verified (OP_CHECKSIG in script, flags don't matter)
- P2PKH kernels: ✅ Verified (OP_CHECKSIG in script, flags don't matter)
- P2WPKH kernels: ❌ **NOT verified** — without `SCRIPT_VERIFY_WITNESS`, `VerifyScript` never enters the witness program detection path (`interpreter.cpp:2035: if (flags & SCRIPT_VERIFY_WITNESS)`). The P2WPKH scriptPubKey `OP_0 <hash>` executes as plain pushes and passes without checking the signature.

**Peercoin's approach is structurally better than Blackcoin's** (passes real witness data, amount, and PrecomputedTransactionData), but it still doesn't verify P2WPKH kernel signatures because the `WITNESS` flag is missing. Peercoin does support P2WPKH kernels in `CreateCoinStake` (`wallet.cpp:3693`), so this is a latent bug in Peercoin too.

**Blackcoin's fix must go further than Peercoin's** by adding `SCRIPT_VERIFY_WITNESS` (and `SCRIPT_VERIFY_TAPROOT` for P2TR).

### 3.8 How Bitcoin Core Does It (for reference)

Bitcoin Core's `CScriptCheck::operator()` (`validation.cpp:2140`):
```cpp
return VerifyScript(scriptSig, m_tx_out.scriptPubKey, witness, nFlags,
    CachingTransactionSignatureChecker(ptxTo, nIn, m_tx_out.nValue, cacheStore,
        *m_signature_cache, *txdata), &error);
```

Bitcoin Core passes:
- `witness` — actual witness data ✅
- `m_tx_out.nValue` — actual UTXO amount ✅
- `*txdata` — PrecomputedTransactionData ✅
- `nFlags` — full consensus flags from `GetBlockScriptFlags()` ✅

### 3.9 The Correct Fix for Blackcoin More

Replace `pos.cpp:157`:

```cpp
// OLD (BUGGY — 3 problems: SCRIPT_VERIFY_NONE, nullptr witness, amount=0):
if (!VerifySignature(coinPrev, txin.prevout.hash, tx, 0, SCRIPT_VERIFY_NONE))
    return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "stake-verify-signature-failed", ...);
```

With:

```cpp
// NEW (FIXED):
{
    PrecomputedTransactionData txdata(tx);
    TransactionSignatureChecker checker(&tx, 0, coinPrev.out.nValue, txdata, MissingDataBehavior::ASSERT_FAIL);
    if (!VerifyScript(txin.scriptSig, coinPrev.out.scriptPubKey, &txin.scriptWitness,
        SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT, checker))
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "stake-verify-signature-failed",
            strprintf("CheckProofOfStake(): VerifyScript failed on coinstake %s", tx.GetHash().ToString()));
}
```

**Three things fixed simultaneously:**
1. **Flags**: `SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT` — enables witness program detection and Taproot verification
2. **Witness**: `&txin.scriptWitness` — passes actual witness data instead of `nullptr`
3. **Amount**: `coinPrev.out.nValue` — passes actual UTXO amount for correct BIP143 sighash

**Why `MissingDataBehavior::ASSERT_FAIL` (not `FAIL`)?**
Bitcoin Core's consensus path (`CScriptCheck` → `CachingTransactionSignatureChecker` at `sigcache.h:70`) uses `ASSERT_FAIL`. Missing data in consensus code indicates a programming error, not a recoverable failure. `ASSERT_FAIL` aborts via assertion (catching bugs immediately in debug builds). `FAIL` silently treats the signature as invalid — acceptable for signing (recoverable) but dangerous for consensus (could mask a bug that rejects valid blocks). The current `VerifySignature` uses `FAIL` — the fix corrects this to `ASSERT_FAIL` matching Bitcoin Core's consensus path.

**Why all three flags?**
- `SCRIPT_VERIFY_P2SH`: Needed for P2SH redeem script execution (also enables P2SH-wrapped SegWit)
- `SCRIPT_VERIFY_WITNESS`: Needed for P2WPKH — without it, `VerifyScript` doesn't detect witness programs and P2WPKH bypasses verification
- `SCRIPT_VERIFY_TAPROOT`: Needed for P2TR — without it, `VerifyWitnessProgram` returns success at line 1920 without checking the Schnorr signature

**Note on `SCRIPT_VERIFY_TAPROOT` here vs §3.11:** Using TAPROOT in `CheckProofOfStake` is different from using it in `MANDATORY_SCRIPT_VERIFY_FLAGS` for mempool/DoS. Here we're verifying a coinstake in a block that's already been accepted — we want to verify all kernel types properly. The TAPROOT flag in the block's `GetBlockScriptFlags()` controls regular transaction verification, which is separate from coinstake kernel verification.

### 3.10 Will This Break Staking? — Definitive Answer

**No. Here's why for each kernel type:**

| Kernel | Will it break? | Why not? |
|---|---|---|
| **P2PK** | ❌ No | Script is not a witness program. `OP_CHECKSIG` verifies the signature regardless of flags. Witness is empty/ignored. Identical behavior before and after. |
| **P2PKH** | ❌ No | Same as P2PK — not a witness program, `OP_CHECKSIG` verifies. Identical behavior. |
| **P2WPKH** | ❌ No | The staking wallet (`staking.cpp:494-512`) creates coinstakes with **correct witness data** via `SignSignature`/`SignTransaction`. The witness is `[DER-sig, compressed-pubkey]` (2 items). The amount is correctly used during signing. Verification with the fix will produce the **same sighash** as signing did, so the signature will match. |
| **P2TR** | ❌ No | The staking wallet creates coinstakes with **correct Schnorr witnesses** via `SignTransaction` with the P2TR descriptor. The `PrecomputedTransactionData` is constructed the same way. Verification will match. |

**The only thing that changes:** Coinstakes with **invalid/empty witnesses** (which currently pass due to the bypass) will now be **rejected**. No valid staking operation produces such coinstakes — only an attacker would.

### 3.11 On-Chain Audit (June 2026)

| Network | SegWit activation | Scanned to | Total coinstakes | Witness kernels | P2WPKH | P2TR | Malformed |
|---|---|---|---|---|---|---|---|
| **Testnet** | 2,070,000 | 2,852,570 | 782,417 | 61,196 | 61,196 | 0 | 0 |
| **Mainnet** | 5,805,000 | 5,928,105 | 123,106 | 3 | 3 | 0 | 0 |

All witness kernels have well-formed `[DER-sig, compressed-pubkey]` data. Zero malformed witnesses. **Zero valid coinstakes will be rejected by the fix.**

### 3.12 Is This a Soft Fork?

**Yes.** The fix only **tightens** rules:
- Blocks with valid P2WPKH/P2TR coinstakes (proper witnesses) → still accepted ✅
- Blocks with invalid P2WPKH/P2TR coinstakes (empty/garbage witnesses) → now rejected (previously accepted) ❌

No block that was invalid before becomes valid after. This is the definition of a soft fork.

**Deployment consideration (Metis review):** All nodes must upgrade before activation, or non-upgraded nodes will accept blocks that upgraded nodes reject (coinstakes with invalid witnesses). This could cause a chain split if not coordinated. Metis recommends using a coordinated activation mechanism (e.g., BIP9-style versionbits or a block-height trigger) with a mandatory upgrade window before enforcement. Cannot deploy as an immediate hard rule change without split risk.

**Note on practical risk:** Since the on-chain audit shows zero malformed P2WPKH witnesses exist on any network, the chain split scenario requires an attacker to deliberately craft a block with an invalid P2WPKH coinstake and broadcast it during the upgrade window. The risk is low but nonzero.

### 3.13 What Does NOT Change

- **`CreateCoinStake`** (`staking.cpp`) — unchanged. The staking wallet already creates correct witnesses.
- **`SignBlock`** (`miner.cpp`) — unchanged. Block signing uses the OP_RETURN carrier, not the kernel signature.
- **`CheckBlockSignature`** (`validation.cpp`) — unchanged. Block signature verification is separate from coinstake input verification.
- **`CheckStakeKernelHash`** (`pos.cpp`) — unchanged. Kernel hash computation doesn't depend on signature verification.
- **Mempool/DoS flags** (`policy.h`) — unchanged. The `SCRIPT_VERIFY_TAPROOT` in `STANDARD_SCRIPT_VERIFY_FLAGS` placement is separate from this fix.

### 3.14 `SCRIPT_VERIFY_TAPROOT` in MANDATORY — POST-ACTIVATION ONLY

Separate from the `CheckProofOfStake` fix: moving `SCRIPT_VERIFY_TAPROOT` to `MANDATORY_SCRIPT_VERIFY_FLAGS` (for mempool/DoS) must wait until **after** mainnet Taproot activation.

**Why it must wait:**
- Before mainnet Taproot activation, `GetBlockScriptFlags()` does NOT add `SCRIPT_VERIFY_TAPROOT` to consensus flags. P2TR is effectively anyone-can-spend at consensus level — a transaction with an empty witness is consensus-valid.
- If TAPROOT were in `MANDATORY` pre-activation, the two-tier mempool check (`validation.cpp:2230-2242`) would: (1) fail STANDARD check (TAPROOT verifies witness → empty witness fails), (2) re-check without `STANDARD_NOT_MANDATORY` flags — but TAPROOT is MANDATORY so it's still included, (3) re-check also fails → returns `TX_CONSENSUS` → **peer gets banned for a consensus-valid transaction**.
- After activation, `GetBlockScriptFlags()` adds `SCRIPT_VERIFY_TAPROOT` to consensus flags. P2TR is no longer anyone-can-spend. Moving TAPROOT to `MANDATORY` is then safe — peers sending invalid P2TR genuinely deserve banning.

**Action:** Move `SCRIPT_VERIFY_TAPROOT` to `MANDATORY_SCRIPT_VERIFY_FLAGS` in a **follow-up release after** mainnet Taproot activation completes. Not a bug — this is the correct sequence.

**Note:** Using `SCRIPT_VERIFY_TAPROOT` in the `CheckProofOfStake` fix (§3.9) is **different** from putting it in `MANDATORY_SCRIPT_VERIFY_FLAGS`. The `CheckProofOfStake` fix verifies coinstake signatures in blocks — it's not a mempool/DoS check. Using TAPROOT there is correct because we want to verify P2TR kernel signatures properly, regardless of whether Taproot is active for regular transactions on mainnet yet.

---

## 4. Taproot Activation Status

| Network | Status |
|---|---|
| **Mainnet** | BIP-9 signaling to start with next release (late 2026), not yet active |
| **Testnet** | `locked_in` since block 2,850,000 → activates at block 2,865,000 |
| **Regtest** | `ALWAYS_ACTIVE` |

The P2WPKH/P2TR signature verification soft fork does **not** depend on Taproot activation — it fixes P2WPKH kernel verification (the only witness type on-chain today). When Taproot activates, P2TR kernels will also be covered by the same fix.

---

## 5. Qtum Comparison Highlights

| Feature | Blackcoin More | Qtum |
|---|---|---|
| Kernel hash | `SHA256(nStakeModifier \|\| blockFromTime \|\| prevout \|\| nTimeTx)` | Identical formula |
| `prevoutStake` in header | No | Yes (enables delegation) |
| `setStakeSeen` anti-dupe | No (mitigated by OP_RETURN timestamp) | Yes (`COutPoint, uint32_t` set) |
| `Coin.nTime` | Yes (serialized in UTXO) | No (removed entirely) |
| Staking loop | 1 window, wake-on-block | 3 windows forward scan |
| Stake cache | Optional (`-stakecache`) | 4 cache types (always on for miner) |
| Delegation | Not supported | EVM-based Proof-of-Delegation |
| Block signature | ECDSA via OP_RETURN carrier or P2PK | ECDSA via P2PK only |
| Timestamp mask | Fixed 0xf (16s) | Adjustable via fork |
| Maturity | 500 blocks (fixed) | 500 blocks (adjustable via fork) |

---

## 6. Key Technical Parameters

| Parameter | Value |
|---|---|
| PoS block spacing | 64 seconds target |
| Timestamp mask | 0xf (16-second boundaries) |
| Coinbase maturity | 500 blocks (mainnet), 10 (testnet) |
| Difficulty adjustment | Per-block EMA (nInterval=15, nTargetTimespan=960s) |
| PoS reward | 1.5 BLK |
| PoW reward | 10,000 BLK (disabled after last PoW block) |
| Combine threshold | 250 BLK |
| Split threshold | 500 BLK |
| Staker sleep floor | 500 + 30√UTXOs ms |
| Future drift | +15 seconds (Protocol V2) |
| MTP | Returns exact previous block timestamp (not median of 11) |
| BIP94 | Not applicable to Blackcoin (per-block EMA difficulty). Disabled on mainnet/testnet, should also be disabled on regtest |

---

## 7. Files Referenced

### Staking / Coinstake
- `src/wallet/staking.cpp` — `CreateCoinStake`, `SelectCoinsForStaking`, input combining
- `src/node/miner.cpp` — `PoSMiner`, `CreateNewBlock`, `SignBlock`, `MsUntilNextWindow`, `SleepStaker`
- `src/pos.cpp` — `CheckProofOfStake`, `CheckKernel`, `CheckStakeKernelHash`

### Validation / Consensus
- `src/validation.cpp` — `CheckBlockSignature`, `ContextualCheckBlockHeader`, `ConnectBlock`
- `src/consensus/tx_check.cpp` — `CheckTransaction`
- `src/consensus/tx_verify.cpp` — `CheckTxInputs`
- `src/pow.cpp` — `CalculateNextTargetRequired`

### Script / Signing
- `src/script/interpreter.cpp` — `VerifyScript`, `VerifyWitnessProgram`, sighash paths
- `src/script/sign.cpp` — `VerifySignature`, `SignTransaction`
- `src/policy/policy.h` — `MANDATORY_SCRIPT_VERIFY_FLAGS`, `STANDARD_SCRIPT_VERIFY_FLAGS`

### Coin / UTXO
- `src/coins.cpp`, `src/coins.h` — `AddCoins`, `Coin` struct (nTime serialization)
- `src/kernel/coinstats.cpp` — MuHash serialization format
- `src/primitives/transaction.h` — v2 nTime stripping

### Wallet
- `src/wallet/wallet.cpp` — `updatedBlockTip`, `ComputeTimeSmart`
- `src/wallet/wallet.h` — `m_last_coin_stake_search_time`, `cv_new_block`

### Network
- `src/netgroup.cpp` — `RelaxNetWorkMask`

---

## 8. Agent Documentation Cross-References

| Document | Topic |
|---|---|
| `agent/validations.md` | Full validation architecture (transaction, block, PoS kernel) |
| `agent/staking.md` | Staking flow analysis, kernel types, OP_RETURN carrier |
| `agent/staking262.md` | v26.2.0 staking walkthrough (historical reference) |
| `agent/staking_probabilities.md` | UTXO selection, combine/split thresholds, expected time formula |
| `agent/StakerTimingRefactor.md` | Single-responsibility timing design (MsUntilNextWindow) |
| `agent/SafetyBump.md` | Wake/sleep design, timer guard, pos_timio |
| `agent/P2TRSigningFix.md` | SCRIPT_VERIFY_TAPROOT missing flag, P2TR signing flow |
| `agent/SegWitTxv2Coinstake.md` | v2 txid collision analysis, OP_RETURN timestamp fix |
| `agent/P2PKMigration.md` | P2PK→P2PKH/P2WPKH/P2TR migration, compatibility matrix |
| `agent/CoinStatsIndexOptimization.md` | nTime recovery, MuHash format change, index rebuild |
| `agent/qtum_comparison.md` | Qtum vs Blackcoin staking architecture comparison |

---

## 9. Cross-Codebase Comparison: Bitcoin Core vs Peercoin vs Qtum vs Blackcoin More

This section compares the Blackcoin More v28.4.0 codebase against its three reference ancestors: Bitcoin Core 28.4.0 (upstream), Peercoin, and Qtum. Blackcoin evolved from Bitcoin Core via Peercoin's PoS design, and Qtum further forked from Blackcoin's PoS lineage. Understanding these relationships clarifies which features are inherited, which are Blackcoin-specific, and which are novel in v28.4.0.

### 9.1 Staking / Proof-of-Stake

| Feature | Bitcoin Core 28.4 | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|---|
| PoS mining | None (PoW only) | `CreateCoinStake` in `wallet/wallet.cpp` | `CreateCoinStake` in `wallet/stake.cpp` | `CreateCoinStake` in `wallet/staking.cpp` |
| PoS miner loop | None | Thread-based polling | Thread-based with forward scan | `PoSMiner` in `node/miner.cpp` with wake-on-block |
| Staking address | N/A | `"mintkey"` label, no purpose enum | `"mintkey"` + delegation | `"SignKey"` + `AddressPurpose::SIGNKEY` enum |
| Block signing | None (PoW only) | P2PK only in `vout[1]` | P2PK + delegation sig in header | OP_RETURN carrier in `vout[1]` |
| Kernel types | N/A | P2PK, P2PKH | P2PK, P2PKH, P2WPKH | P2PK, P2PKH, P2WPKH, P2TR |
| Staker wake | N/A | No wake-on-block | No wake-on-block | `cv_new_block` + `MsUntilNextWindow()` |

### 9.2 Block Validation & Consensus

| Feature | Bitcoin Core 28.4 | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|---|
| `CheckBlockSignature` | None | P2PK only (`validation.cpp:4994`) | P2PK only | P2PK + OP_RETURN carrier (`validation.cpp:3860`) |
| PoS timestamp mask | N/A | `0xf` (16s) | `0xf` (adjustable via fork) | `0xf` (16s, fixed) |
| Coinstake timestamp | N/A | `nTimeBlock == nTimeTx` | `nTimeBlock & mask == 0` only | `nTimeBlock == nTimeTx && (nTimeTx & 0xf) == 0` |
| Difficulty adjustment | Every 2016 blocks | Per-block EMA | Per-block EMA (adjustable) | Per-block EMA (`nInterval=15`, `nTargetTimespan=960s`) |
| BIP94 (timewarp) | Not applicable | Not applicable | Not applicable | **Not applicable** — Blackcoin uses per-block EMA difficulty, not fixed intervals. Code exists but disabled everywhere; should also be disabled on regtest |
| `setStakeSeen` anti-dupe | N/A | No | **Yes** — `(prevoutStake, nTime)` set | No (mitigated by OP_RETURN timestamp) |
| MTP calculation | Median of 11 blocks | Exact previous block timestamp | Exact previous block timestamp | Exact previous block timestamp (Protocol V2) |

### 9.3 Coin/UTXO Structure

| Feature | Bitcoin Core 28.4 | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|---|
| `Coin.nTime` | **Not present** | Not checked in code | **Not present** | `unsigned int nTime` (serialized) |
| `Coin.fCoinStake` | Not present | Not present | Not present | **Present** (serialized in MuHash) |
| `blockFromTime` source | N/A | `blockFrom->nTime` | `blockFrom->nTime` | `coinPrev.nTime ? coinPrev.nTime : blockFrom->nTime` |
| v2 tx `nTime` | Not applicable | Not applicable | Not applicable | In-memory only, stripped from serialization (`transaction.h:233-236`) |

### 9.4 Transaction `nTime`

| Feature | Bitcoin Core 28.4 | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|---|
| `CTransaction.nTime` | **Not present** | Present, serialized | Not present | Present, **stripped for v2** (`version >= 2` → `nTime = 0` on deserialize) |
| v2 txid collision | N/A | N/A (nTime serialized) | N/A (no nTime field) | **Yes** — same UTXO + same outputs = same txid. Fixed by OP_RETURN timestamp |

### 9.5 Script Verification Flags

| Flag | Bitcoin Core MANDATORY | Peercoin | Qtum | Blackcoin More MANDATORY | Blackcoin More STANDARD |
|---|---|---|---|---|---|
| `SCRIPT_VERIFY_P2SH` | ✅ | ✅ | ✅ | ✅ | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_DERSIG` | ✅ | ✅ | ✅ | ✅ | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_DERKEY` | ❌ (doesn't exist) | ❌ | ❌ | ✅ **(Blackcoin-specific)** | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_LOW_S` | ✅ (STANDARD only) | ✅ | ✅ | ✅ **(MANDATORY)** | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY` | ✅ | ✅ | ✅ | ✅ | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` | ✅ | ✅ | ✅ | ✅ **(newly added)** | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_WITNESS` | ✅ | ✅ | ✅ | ✅ **(newly added)** | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_TAPROOT` | ✅ (MANDATORY) | ❌ | ✅ | ❌ | ✅ **(STANDARD only, not mandatory until mainnet activates)** |
| `SCRIPT_VERIFY_NULLDUMMY` | ✅ | ✅ | ✅ | ✅ | ✅ (via MANDATORY) |

**Key differences:**
- `SCRIPT_VERIFY_DERKEY` (bit 31) is a Blackcoin-specific flag not present in Bitcoin Core, Peercoin, or Qtum. It enforces DER encoding for public keys in scripts.
- `SCRIPT_VERIFY_LOW_S` is mandatory (bannable) in Blackcoin but only standard (non-bannable) in Bitcoin Core.
- `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` and `SCRIPT_VERIFY_WITNESS` were missing from Blackcoin's MANDATORY flags before this release (SegWit merge gap), now added.
- `SCRIPT_VERIFY_TAPROOT` is STANDARD-only in Blackcoin (not bannable until mainnet activation), while Bitcoin Core has it in MANDATORY (since Taproot is always active on Bitcoin).

### 9.6 CheckProofOfStake Signature Verification

| Codebase | Function Call | Witness Data | Flags | P2PK verified? | P2PKH verified? | P2WPKH verified? | P2TR verified? |
|---|---|---|---|---|---|---|---|
| **Blackcoin More** | `VerifySignature(coinPrev, ..., tx, 0, SCRIPT_VERIFY_NONE)` | `nullptr` | `SCRIPT_VERIFY_NONE` | ✅ Yes | ✅ Yes | ❌ No (trivial pass) | ❌ No (trivial pass) |
| **Peercoin** | `VerifyScript(..., &witness, SCRIPT_VERIFY_P2SH, ...)` | Real witness | `SCRIPT_VERIFY_P2SH` | ✅ Yes | ✅ Yes | ❌ No (witness not checked — `WITNESS` flag missing) | N/A |
| **Qtum** | `VerifySignature(..., SCRIPT_VERIFY_NONE)` → `VerifyScript(..., nullptr, 0, ...)` | `nullptr` | `SCRIPT_VERIFY_NONE` | ✅ Yes | ✅ Yes | ❌ No (trivial pass) | ❌ No (trivial pass) |

**Security implication:** All three codebases (Blackcoin More, Peercoin, Qtum) fail to verify P2WPKH kernel signatures at consensus level. Peercoin passes the real witness data and the amount, but uses only `SCRIPT_VERIFY_P2SH` — without `SCRIPT_VERIFY_WITNESS`, `VerifyScript` never enters the witness program detection path (`interpreter.cpp:2035: if (flags & SCRIPT_VERIFY_WITNESS)`). The P2WPKH scriptPubKey `OP_0 <hash>` executes as plain pushes and passes without checking the signature.

**The correct fix requires `SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT`** (all three flags). Peercoin's `SCRIPT_VERIFY_P2SH`-only approach is **insufficient** for P2WPKH verification — it's structurally better than Blackcoin's approach (passes real witness data and amount) but still doesn't activate the witness verification path.

**Proposed fix for Blackcoin More** (see §3.9 of this review): Change `pos.cpp:157` to call `VerifyScript` directly with real witness data, the actual UTXO amount, `PrecomputedTransactionData`, and `SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT`. This is a safe soft fork — the on-chain audit confirms zero malformed P2WPKH witnesses.

### 9.7 Block Header Structure

| Field | Bitcoin Core | Peercoin | Qtum | Blackcoin More |
|---|---|---|---|---|
| `prevoutStake` | N/A | N/A | **Present** (`COutPoint`) | N/A |
| `vchBlockSigDlgt` | N/A | N/A | **Present** (delegation sig) | N/A |
| `nStakeModifier` | N/A | Computed per-block | Computed per-block | Computed per-block (stored in `CBlockIndex`) |

Qtum's `prevoutStake` in the block header enables delegation (signing with a different key than the kernel UTXO owner). Blackcoin does not support delegation — the kernel UTXO owner must also sign the block.

### 9.8 Wallet / Address Types

| Feature | Bitcoin Core 28.4 | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|---|
| `AddressPurpose` enum | RECEIVE, SEND, REFUND | RECEIVE, SEND, REFUND | RECEIVE, SEND, REFUND | RECEIVE, SEND, REFUND, **SIGNKEY** |
| Default address type | Bech32 (P2WPKH) | Legacy (P2PKH) | Legacy (P2PKH) | **Bech32 (P2WPKH)** (changed from LEGACY in v28.4) |
| Staking address lookup | N/A | `"mintkey"` string label | `"mintkey"` string label | `"SignKey"` + `AddressPurpose::SIGNKEY` |
| Descriptor wallet | Yes | Yes | Yes | Yes (supports P2TR staking) |
| Legacy wallet | Yes | Yes | Yes | Yes (P2PK/P2PKH staking only; P2TR not mineable) |

### 9.9 Network / Time Configuration

| Feature | Bitcoin Core 28.4 | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|---|
| `WARN_THRESHOLD` (clock sync) | **10 minutes** | N/A | N/A | **16 seconds** |
| `FutureDrift` | N/A | N/A | N/A | **15 seconds** (Protocol V2) |
| `RelaxNetWorkMask` | N/A | N/A | N/A | **Present** (defaults to `true` — intentional testing choice, will be `false` for production) |
| Block spacing target | 600 seconds (10 min) | ~600 seconds | ~64 seconds (PoS) | **64 seconds** |
| Coinbase maturity | 100 blocks | 500 blocks | 500 blocks (variable) | **500 blocks** (mainnet), 10 (testnet) |

### 9.10 Deployment / Fork Handling

| Deployment | Bitcoin Core 28.4 | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|---|
| SegWit (BIP141) | Buried (always active) | N/A | Active | **Buried** (always active, moved from version-bits) |
| CSV (BIP112) | Buried (always active) | N/A | Active | **Buried** (always active) |
| Taproot (BIP341/342) | **Buried** (always active) | N/A | Active | **BIP9 signaling** (not yet active on mainnet; testnet locked_in) |
| DERKEY | N/A | N/A | N/A | **Always active** (Blackcoin-specific, no deployment) |

### 9.11 Staker Timing / Wake Mechanism

| Feature | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|
| Wake-on-block | **No** (polls `pos_timio`) | **No** (polls `nMinerSleep`) | **Yes** (`cv_new_block` + `MsUntilNextWindow()`) |
| Timer guard scope | Global static | Per-wallet | Per-wallet (`m_last_coin_stake_search_time`) |
| Window scan | Current window only | Forward 3 windows (48s lookahead) | Current window only |
| Post-block rest | Fixed sleep | Spin-wait until target time | `MsUntilNextWindow()` (boundary-aligned) |
| `pos_timio` | `500 + 30√UTXOs` ms | `5000ms` (or `20000ms` min diff) | `500 + 30√UTXOs` ms (floor for failed search) |

### 9.12 Staking Output / Reward Structure

| Feature | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|
| Block signing output | P2PK in `vout[1]` | P2PK in `vout[1]` | **OP_RETURN carrier** in `vout[1]` |
| Reward output type | Always P2PK (converted from P2PKH) | Always P2PK | **Native kernel type** (P2PK→P2PKH, P2PKH, P2WPKH, P2TR) |
| `bMinterKey` flag | Present (witness kernel intermediate) | Present | **Removed** (all types use same carrier) |
| Split threshold | RFC28 security level | Variable | **500 BLK** (2 × 250 BLK combine threshold) |
| Combine threshold | Variable | Variable | **250 BLK** |
| Coinbase output | 1 output (empty) or 2 outputs (w/ SegWit commitment) | 1 or 2 | 1 (empty) or 2 (w/ SegWit commitment) — same as Bitcoin |
| Dev fund | N/A | N/A | **Optional** (`vout[4]` if enabled) |

### 9.13 Key Architectural Differences — Summary

**From Bitcoin Core (upstream):**
- Blackcoin adds PoS consensus (staking, coinstake, kernel hash, stake modifier)
- Adds `nTime` field to transactions and UTXOs (for PoS timestamp alignment)
- Adds `SCRIPT_VERIFY_DERKEYS` (Blackcoin-specific mandatory flag)
- Moves `CHECKSEQUENCEVERIFY` and `WITNESS` into `MANDATORY_SCRIPT_VERIFY_FLAGS`
- Keeps `TAPROOT` in `STANDARD` only (not mandatory until mainnet activation)
- Changes `WARN_THRESHOLD` from 10 minutes to 16 seconds (matching PoS timing)
- Adds `AddressPurpose::SIGNKEY` for staking address management
- Adds `RelaxNetWorkMask` (currently defaults to `true` — intentional testing choice, will be `false` for production release)
- Buries SegWit as a deployment (no longer version-bits signaled)

**From Peercoin (PoS ancestor):**
- Blackcoin uses OP_RETURN carrier for block signing (Peercoin uses P2PK only)
- Blackcoin supports P2WPKH and P2TR kernel types (Peercoin supports P2PK and P2PKH only)
- Blackcoin's `CheckBlockSignature` accepts OP_RETURN (Peercoin rejects non-PUBKEY)
- Peercoin passes real witness data + `SCRIPT_VERIFY_P2SH` in `CheckProofOfStake` — structurally better than Blackcoin but still insufficient (missing `SCRIPT_VERIFY_WITNESS` flag, so P2WPKH kernels are not verified in Peercoin either)
- Blackcoin uses per-wallet timer guard (Peercoin uses global static)
- Blackcoin uses wake-on-block with condition variable (Peercoin polls)

**From Qtum (PoS fork):**
- Qtum adds `prevoutStake` to block header for delegation — Blackcoin does not
- Qtum has `setStakeSeen` anti-duplicate mechanism — Blackcoin mitigates via OP_RETURN timestamp
- Qtum removed `nTime` from `Coin` struct — Blackcoin keeps it (with recovery logic for v2 txs)
- Qtum uses 3-window forward scan — Blackcoin uses single-window with wake-on-block
- Qtum has 4 stake cache types — Blackcoin has optional `-stakecache`
- Both use `SCRIPT_VERIFY_NONE` with `nullptr` witness in `CheckProofOfStake` — both have the P2WPKH/P2TR signature verification gap

---

## 10. Documentation Accuracy Audit — agent/ Files vs Source Code

Verified all agent/ markdown files against the actual Blackcoin More source code. Findings:

### 10.1 Verified Accurate

| Claim | Source File | Verification |
|---|---|---|
| `CheckProofOfStake` at `pos.cpp:130` | `src/pos.cpp` line 130 | ✅ Match |
| `VerifySignature` with `SCRIPT_VERIFY_NONE` at `pos.cpp:157` | `src/pos.cpp` line 157 | ✅ Match |
| `CheckBlockSignature` at `validation.cpp:3860` | `src/validation.cpp` line 3860 | ✅ Match |
| `nStakeTimestampMask = 0xf` in chainparams | `src/kernel/chainparams.cpp` lines 140, 257, 501, 579 | ✅ All set to `0xf` |
| v2 `nTime` stripping at `transaction.h:233-236, 277-278` | `src/primitives/transaction.h` lines 233-236, 277-278 | ✅ Match |
| `SCRIPT_VERIFY_DERKEY` bit 31 in `interpreter.h:105` | `src/script/interpreter.h` line 105 | ✅ Match |
| `SCRIPT_VERIFY_DERKEY` in `MANDATORY_SCRIPT_VERIFY_FLAGS` | `src/policy/policy.h` line 92 | ✅ Match |
| `SCRIPT_VERIFY_LOW_S` in MANDATORY | `src/policy/policy.h` line 94 | ✅ Match |
| `bMinterKey` removed from staking.cpp and miner.cpp | grep confirms zero matches | ✅ Fully removed |
| OP_RETURN carrier in `CheckBlockSignature` reads exactly 2 GetOps | `src/validation.cpp` lines 3879-3892 | ✅ Match |
| `Coin.nTime` field exists in `coins.h` | `src/coins.h` | ✅ Present |
| `Coin.fCoinStake` serialization in MuHash | `src/kernel/coinstats.cpp` | ✅ Present |
| `AddressPurpose::SIGNKEY` in `types.h` | `src/wallet/types.h` line 65 | ✅ Match |
| `g_relax_network_mask = true` in `netgroup.cpp` (intentional testing choice) | `src/netgroup.cpp` line 15 | ✅ Confirmed present — intentional, not a bug |
| `WARN_THRESHOLD` changed to 16s in `timeoffsets.h` | `src/node/timeoffsets.h` | ✅ Match |
| `MsUntilNextWindow()` implementation matches SafetyBump.md | `src/node/miner.cpp` lines 54-63 | ✅ Match |
| `SleepStaker()` implementation matches SafetyBump.md | `src/node/miner.cpp` lines 604-630 | ✅ Match |
| Peercoin `CheckBlockSignature` P2PK-only at `validation.cpp:4994` | `/peercoin/src/validation.cpp` line 4994 | ✅ Verified |
| Peercoin `VerifyScript` with real witness at `kernel.cpp:691` | `/peercoin/src/kernel.cpp` line 691 | ✅ Verified |
| Qtum `setStakeSeen` at `validation.cpp:127, 6192` | `/qtum/src/validation.cpp` lines 127, 6192 | ✅ Verified |
| Qtum `prevoutStake` in `block.h:34,42` | `/qtum/src/primitives/block.h` lines 34, 42 | ✅ Verified |
| Qtum `Coin` struct has no `nTime` field | `/qtum/src/coins.h` | ✅ Verified |
| Bitcoin Core `Coin` struct has no `nTime` field | `/bitcoin/src/coins.h` | ✅ Verified |
| Bitcoin Core `CTransaction` has no `nTime` field | `/bitcoin/src/primitives/transaction.h` | ✅ Verified |
| Bitcoin Core `WARN_THRESHOLD = 10 minutes` | `/bitcoin/src/node/timeoffsets.h` | ✅ Verified |

### 10.2 Minor Discrepancies (Low Severity)

| Document | Claim | Reality | Impact |
|---|---|---|---|
| `agent/SafetyBump.md` | `MsUntilNextWindow` returns `std::max(0LL, ...)` | Actual code: `std::max<int64_t>(0, ...)` | None — functionally identical. The `int64_t` cast is more explicit; `0LL` would also work. |
| `agent/validations.md` | Line numbers (e.g., `bad-txns-vin-empty` at line 14) | Actual: line 15 in `tx_check.cpp` | Low — line numbers shift with edits, behavioral claims are correct |
| `agent/staking.md` §9 | `VerifySignature` at `sign.cpp:734` | Not verified in this pass (would need to check current line) | Low — the function exists and behavior described is correct |
| `agent/staking262.md` | Describes v26.2.0 behavior | Marked as "Historical reference" | None — explicitly documented as historical |

### 10.3 Cross-Codebase Comparison Verified

All claims in §9 (cross-codebase comparison) verified against source code:

- Bitcoin Core 28.4.0 has no `nTime`, no PoS, no `Coin.nTime`, `WARN_THRESHOLD = 10min`, no `DERKEY` flag
- Peercoin's `CheckBlockSignature` is P2PK-only (verified at line 4994-5010)
- Peercoin passes real witness data with `SCRIPT_VERIFY_P2SH` in `CheckProofOfStake` (verified at `kernel.cpp:691`) — but missing `SCRIPT_VERIFY_WITNESS` flag means P2WPKH kernels are still not verified in Peercoin
- Qtum has `prevoutStake` in block header (verified), `setStakeSeen` (verified), no `Coin.nTime` (verified)
- Both Qtum and Blackcoin use `SCRIPT_VERIFY_NONE` with `nullptr` witness (verified)
- Blackcoin's `MANDATORY_SCRIPT_VERIFY_FLAGS` now includes `CSV` and `WITNESS` (verified at `policy.h:91-98`)
- Blackcoin's `STANDARD_SCRIPT_VERIFY_FLAGS` includes `TAPROOT` but not in `MANDATORY` (verified at `policy.h:106-119`)

### 10.4 Conclusion

All agent/ documentation files are **substantially accurate**. The only discrepancies are minor line number offsets (inevitable with ongoing code changes) and one trivial type notation difference (`0LL` vs `int64_t(0)`). No behavioral claims, code logic descriptions, or architectural assertions were found to be incorrect.

---

## 11. Slop Removal — Completed and Staged

### 11.1 `src/wallet/test/fuzz/fees.cpp` — Dead code removed (26 lines)

Removed commented-out code blocks referencing removed Blackcoin APIs:
- `m_fallback_fee` initialization (removed API)
- `tx_bytes` variable declaration (unused — `GetRequiredFee(wallet, tx_bytes)` was removed)
- `m_min_fee` initialization (removed API)
- `GetRequiredFee(wallet, tx_bytes)` call (removed API)
- `/* */` block containing `m_confirm_target`, `m_fee_mode`, `FeeCalculation`, `GetMinimumFeeRate`, `GetMinimumFee` (all removed or commented out)

### 11.2 `src/qt/bitcoingui.cpp` — Dead comment removed + style fix

- Removed `// frameBlocksLayout->addWidget(unitDisplayControl);` (dead code — widget removed from layout)
- Fixed `// blackcoin::` → `// blackcoin:` for style consistency

### 11.3 `src/rpc/blockchain.cpp` — Style fix (2 instances)

- Fixed `// blackcoin:: include flags` → `// blackcoin: include flags`
- Fixed `// blackcoin:: include modifier` → `// blackcoin: include modifier`

### 11.4 `src/wallet/staking.cpp` — Style fix

- Fixed `// blackcoin:: Optimization` → `// blackcoin: Optimization`

### 11.5 `src/node/timeoffsets.cpp` — UB-safety comment restored

The original Bitcoin Core comment `// when median == std::numeric_limits<int64_t>::min(), calling std::chrono::abs is UB` was replaced by the Blackcoin diff with a comment about the 16s threshold, losing the UB-safety rationale for the `std::max` clamp. Restored the guard comment:
```cpp
// blackcoin: warn when median offset exceeds 16s — matches FutureDrift consensus limit.
// Guard: std::chrono::abs(int64_t::min()) is UB, so clamp to min+1 first.
```

### 11.6 `src/wallet/rpc/spend.cpp` — `burn` RPC info leak fixed

Moved `EnsureWalletIsUnlocked(*pwallet)` before `GetBalance` call in the `burn` RPC handler. Previously, a locked wallet user could distinguish "insufficient funds" from "wallet locked", leaking balance information.

---

## 12. Acknowledged Items — Not Planned for Implementation

These architectural differences vs Qtum/Peercoin are acknowledged and documented. No implementation planned — they are design choices, not defects.

### 12.1 No `setStakeSeen` anti-duplicate mechanism

Qtum has `setStakeSeen` (`COutPoint, uint32_t` set) to reject duplicate stakes at the block header level. Blackcoin mitigates the txid collision via the OP_RETURN timestamp carrier instead. Both approaches could coexist, but the OP_RETURN fix addresses the collision at the txid level which is sufficient for Blackcoin's use case.

**Reference**: `agent/SegWitTxv2Coinstake.md` §Option A, `agent/qtum_comparison.md` §5

### 12.2 No delegation support

Qtum supports Proof-of-Delegation via `prevoutStake` in block header + EVM smart contracts, allowing offline staking and super stakers. Blackcoin requires the kernel UTXO owner to sign the block directly. This is a deliberate design choice — Blackcoin does not have an EVM layer and delegation is not part of the roadmap.

**Reference**: `agent/qtum_comparison.md` §12

### 12.3 No forward-scan staking windows

Qtum scans 3 windows ahead (48s lookahead) with a pre-populated cache. Blackcoin scans only the current window with wake-on-block via `cv_new_block` + `MsUntilNextWindow()`. Blackcoin's approach is simpler, lower CPU, and sufficient for its block spacing. Qtum's forward scan may produce blocks marginally faster under ideal conditions but adds complexity.

**Reference**: `agent/qtum_comparison.md` §7

### 12.4 CoinStatsIndex rebuild performance — as designed

The CoinStatsIndex rebuild is slow (same slowness as Bitcoin Core and Qtum). This is inherent to the indexing design, not a bug. No optimization planned — the index rebuilds correctly, just takes time.

**Reference**: `agent/CoinStatsIndexOptimization.md`

### 12.5 `ExtractDestination` P2PK hack — accepted, future removal

`addresstype.cpp:67-74` reinterprets P2PK scripts as P2PKH addresses for display. This is still needed for legacy P2PK reward UTXOs in the combining pool. Will be removed when native staking is possible for all address types (P2PKH, P2WPKH, P2TR) without the P2PK intermediate. This is a large task and not planned for the near term.

**Reference**: `agent/staking.md` §"ExtractDestination Hack"

### 12.6 BIP94 not applicable to Blackcoin — should be fully disabled

BIP94 is a timewarp-attack mitigation designed for Bitcoin's fixed-interval difficulty adjustment. Blackcoin uses a per-block exponential moving average (EMA) difficulty adjustment — the difficulty recalculates at every block boundary based on the time between the last two PoS blocks. Because the difficulty changes continuously rather than at fixed period boundaries, the timewarp attack vector that BIP94 protects against does not apply.

The BIP94 code currently exists but is disabled on mainnet and testnet (`enforce_BIP94 = false`). It should **also be disabled on regtest** for consistency, since the EMA difficulty model is used on all networks.

**Action:** Set `enforce_BIP94 = false` on regtest as well. Remove or guard the BIP94-related difficulty calculation at `pow.cpp:78-84` to ensure it never executes on any network.

**Reference**: `agent/validations.md` §3, `agent/staking.md` §10

### 12.7 Header sync PRESYNC phase — effectively a no-op for Blackcoin

**Status:** Acknowledged. Not planned for modification — the overhead is accepted as-is.

Bitcoin Core's headers-first sync protocol (inherited by Blackcoin) uses a two-phase header synchronization:

1. **PRESYNC** — lightweight validation: stores 1-bit hash commitments per 50 headers, checks difficulty transitions, accumulates chain work
2. **REDOWNLOAD** — re-downloads full headers, verifies the hash commitments match, then promotes to `AcceptBlockHeader` for full validation

The PRESYNC phase's primary security mechanism is `PermittedDifficultyTransition` (`pow.cpp:101`), which prevents an adversary from claiming arbitrary difficulty jumps between consecutive headers. In Bitcoin, this check constrains the claimed work to realistic bounds — an attacker cannot compress infinite work into a short chain because difficulty can't jump more than 4x per block.

**For Blackcoin, `PermittedDifficultyTransition` is stubbed out:**

```cpp
// pow.cpp:101
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    // Blackcoin: skip this check as we are using different difficulty adjustment algo
    return true;
}
```

**Why it must be disabled:** Blackcoin has **separate PoW and PoS difficulty tracks**. `GetNextTargetRequired` calls `GetLastBlockIndex(pindexLast, fProofOfStake)`, which walks backward to the last block of the same type. Since PoW and PoS blocks alternate on the same chain, consecutive headers routinely have dramatically different `nBits` values — a PoS block may have 1/100th the target of the preceding PoW block. A Bitcoin-style "difficulty can't change by more than 4x" check would reject every valid PoW→PoS transition.

**What PRESYNC still does for Blackcoin (marginal value):**

| Function | Value for Blackcoin |
|---|---|
| `PermittedDifficultyTransition` | **None** — always returns `true` |
| Hash commitments (1 bit per 50 headers) | **Marginal** — prevents the *same* peer from switching chains between PRESYNC and REDOWNLOAD. Does not protect against a fully malicious peer who lies consistently in both phases. |
| Work accumulation (`GetBlockProof`) | **Needed** — gates the `min_pow_checked` threshold in `AcceptBlockHeader`. But `GetBlockProof` uses `nBits` directly, so an adversary can inflate claimed work by setting tiny targets. |
| Chain length bounding (`max_commitments`) | **Useful** — soft cap at ~53k headers per peer, limiting memory consumption. |

**Practical impact:** On mainnet IBD from scratch, the PRESYNC + REDOWNLOAD pipeline takes approximately **30 minutes** before block download can begin. During this time, the node is serially processing 2000-header batches (~400ms per round-trip), storing hash commitments, and accumulating work — all without any Blackcoin-specific validation. The actual header validation (PoW for PoW blocks via `CheckProofOfWork`, difficulty via `GetNextTargetRequired`, PoS kernel via `CheckProofOfStake`) happens downstream in `AcceptBlockHeader` and `ConnectBlock`, regardless of whether PRESYNC ran.

**Why we leave it as-is:** Removing PRESYNC would require replacing the anti-DoS work threshold mechanism and the hash commitment scheme with an alternative. The 30-minute overhead is a one-time cost during IBD and does not affect steady-state operation. The security model is unchanged — PRESYNC doesn't add Blackcoin-specific validation, and its removal wouldn't remove any protection that matters for Blackcoin's threat model. The complexity of modifying the inherited headersync protocol outweighs the benefit of saving 30 minutes during initial sync.

**Reference**: `agent/validations.md` §2, §3 (header validation flow), `src/pow.cpp:101-105`, `src/headerssync.cpp:178-214` (PRESYNC), `src/headerssync.cpp:216-278` (REDOWNLOAD), `src/net_processing.cpp:3003-3083` (`IsContinuationOfLowWorkHeadersSync`)

---

## 13. Implementation Plan: P2WPKH / P2TR Signature Verification Soft-Fork

This implementation plan outlines the fix for the critical consensus vulnerability where `CheckProofOfStake` bypasses signature verification for SegWit and Taproot staking kernels.

### 13.1 Background Context
Currently, `CheckProofOfStake` passes `SCRIPT_VERIFY_NONE` and an empty witness (`nullptr`) when verifying the coinstake kernel. While this safely checked legacy P2PK and P2PKH (because they require OP_CHECKSIG in the scriptSig execution), it trivially passes P2WPKH and P2TR without verifying the cryptographic signature.

The fix goes beyond Peercoin's approach. Peercoin passes real witness data and the correct amount, but uses only `SCRIPT_VERIFY_P2SH` — missing `SCRIPT_VERIFY_WITNESS`, which means P2WPKH kernels are still not verified in Peercoin either. Blackcoin's fix must include all three flags: `SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT`.

### 13.2 Proposed Changes

#### [MODIFY] [pos.cpp](file:///mnt/data/Development/Blackcoin/blackcoin-more-bdev/src/pos.cpp)
- **Add Include:** Add `#include <script/interpreter.h>` to access `TransactionSignatureChecker` and `VerifyScript`.
- **Refactor `CheckProofOfStake` Signature Validation:**
  Remove the unsafe wrapper call:
  ```cpp
  // REMOVE:
  if (!VerifySignature(coinPrev, txin.prevout.hash, tx, 0, SCRIPT_VERIFY_NONE))
      return state.Invalid(...);
  ```
  Replace it with direct script verification:
  ```cpp
  // NEW (FIXED — three things fixed: flags, witness, amount):
  PrecomputedTransactionData txdata(tx);
  TransactionSignatureChecker checker(&tx, 0, coinPrev.out.nValue, txdata, MissingDataBehavior::ASSERT_FAIL);
  if (!VerifyScript(txin.scriptSig, coinPrev.out.scriptPubKey, &txin.scriptWitness,
      SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT, checker)) {
      return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "stake-verify-signature-failed",
          strprintf("CheckProofOfStake(): VerifyScript failed on coinstake %s", tx.GetHash().ToString()));
  }
  ```

  **Three things fixed simultaneously:**
  1. **Flags**: `SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT` — enables witness program detection (WITNESS) and Taproot Schnorr verification (TAPROOT). Without WITNESS, P2WPKH bypasses. Without TAPROOT, P2TR bypasses (VerifyWitnessProgram returns success at line 1920).
  2. **Witness**: `&txin.scriptWitness` — passes actual witness data instead of `nullptr`.
  3. **Amount**: `coinPrev.out.nValue` — passes actual UTXO amount for correct BIP143 sighash. The current code passes 0, which would produce wrong sighashes for P2WPKH.

  **Why `SCRIPT_VERIFY_TAPROOT` is safe here (unlike in MANDATORY_SCRIPT_VERIFY_FLAGS):**
  Using TAPROOT in `CheckProofOfStake` verifies coinstake signatures in blocks — it's not a mempool/DoS check. P2TR coinstakes only exist when Taproot is active on that network. On networks where Taproot is not yet active, no P2TR coinstakes exist, so the flag has no effect. This is separate from putting TAPROOT in `MANDATORY_SCRIPT_VERIFY_FLAGS` (§3.14), which must wait for mainnet activation to avoid false peer bans.

### 13.3 Verification Plan — Phased Deployment

The fix must be tested on regtest and testnet **without BIP-9 deployment** first, before implementing BIP-9-style activation for mainnet. This phased approach allows confidence-building through real-world validation before any consensus change goes live on mainnet.

**The code change is the same for all three phases** — the `pos.cpp:157` fix from §3.9. The only difference is **how and when it's activated**:

- **Phase 1/2**: The fix is applied unconditionally. No BIP-9 gate. All nodes running the binary enforce strict verification immediately.
- **Phase 3**: The fix is gated by BIP-9 signaling. Nodes only enforce strict verification after BIP-9 activation completes. During the signaling period, the fix is compiled in but not enforced.

This means the initial implementation (Phase 1/2) is simpler — no BIP-9 infrastructure needed. Phase 3 adds the BIP-9 gate later.

#### Phase 1: Regtest Testing (no BIP-9, immediate activation on regtest)

Regtest is a controlled environment — the fix can be activated immediately without BIP-9 coordination. The code change from §3.9 is applied directly to `pos.cpp`. No BIP-9 gate, no versionbits logic. The fix is always active on regtest.

1. **Compile and run** the node with the fix applied to `pos.cpp`
2. **Verify regtest node syncs** from genesis with the new strict verification
3. **P2PK staking test**: Create a wallet, fund it with P2PK UTXO, mine blocks, verify coinstakes are accepted
4. **P2PKH staking test**: Create a wallet, fund it with P2PKH UTXO, mine blocks, verify coinstakes are accepted
5. **P2WPKH staking test**: Create a wallet, fund it with P2WPKH UTXO, mine blocks, verify coinstakes are accepted (this validates the `amount` parameter fix)
6. **P2TR staking test**: Create a wallet, fund it with P2TR UTXO, mine blocks, verify coinstakes are accepted (this validates the `SCRIPT_VERIFY_TAPROOT` and `PrecomputedTransactionData` changes)
7. **Mixed kernel types**: Stake a block with a P2WPKH kernel and a P2PK reward, verify it's accepted
8. **Run `make check`**: All unit tests pass
9. **Run `test/pos_tests.cpp`**: All PoS-specific tests pass

#### Phase 2: Testnet Testing (no BIP-9, immediate activation on testnet)

Testnet is a public development network. Apply the same code change from §3.9 directly to `pos.cpp` for the testnet binary. No BIP-9 gate — the fix is always active on testnet. This is safe because testnet is a development network where breaking the chain is acceptable and expected.

1. **Build and release** a testnet binary with the fix
2. **Deploy to testnet nodes** — the on-chain audit confirms all 61,196 existing P2WPKH testnet coinstakes have valid witnesses, so historical sync should succeed
3. **Sync verification**: A testnet node running the fix must sync the full chain (782,417 coinstakes including 61,196 P2WPKH) without rejection
4. **Staking regression**: All four kernel types (P2PK, P2PKH, P2WPKH, P2TR) must successfully stake on testnet
5. **Monitor for rejections**: If any historical block is rejected, that's a critical bug — investigate before proceeding
6. **Run testnet for 2+ weeks**: Generate new coinstakes under the new rules, confirm consensus holds

#### Phase 3: Mainnet Deployment (BIP-9 activation)

Only after Phase 1 and Phase 2 succeed without issues. In this phase, the same code change is applied to `pos.cpp`, but **gated by a BIP-9 versionbits flag**. During the signaling period, the fix is compiled in but not enforced — the code checks the BIP-9 activation state and falls back to the old `VerifySignature` call if not yet activated.

```cpp
// Phase 3: BIP-9 gated version
if (IsBIP9StrictPOSVerificationActive(pindexPrev)) {
    // New strict verification (§3.9)
    PrecomputedTransactionData txdata(tx);
    TransactionSignatureChecker checker(&tx, 0, coinPrev.out.nValue, txdata, MissingDataBehavior::ASSERT_FAIL);
    if (!VerifyScript(txin.scriptSig, coinPrev.out.scriptPubKey, &txin.scriptWitness,
        SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT, checker)) {
        return state.Invalid(...);
    }
} else {
    // Old behavior during signaling period
    if (!VerifySignature(coinPrev, txin.prevout.hash, tx, 0, SCRIPT_VERIFY_NONE)) {
        return state.Invalid(...);
    }
}
```

1. **Implement BIP-9 versionbits deployment** for `CheckProofOfStake` strict verification
2. **Signal period**: Standard BIP-9 signaling (e.g., 2016-block periods, 1916-block threshold, 4032-block timeout)
3. **Activation gate**: Only enforce strict verification after BIP-9 activation completes
4. **Mandatory upgrade window**: Give node operators time to upgrade before activation
5. **Post-activation monitoring**: Watch for any chain split or consensus issues

#### Regression Tests (to add in Phase 1)

- **Malformed witness rejection**: Test that a coinstake with an empty P2WPKH witness is rejected
- **Wrong amount rejection**: Test that a P2WPKH coinstake signed with the wrong amount is rejected (validates the `amount=0` bug fix)
- **Missing P2TR precomputed data**: Test that P2TR without `PrecomputedTransactionData` triggers `ASSERT_FAIL`
- **P2SH-wrapped SegWit**: Test P2SH(P2WPKH) kernel behavior if any exist
- **Witness malleability**: Test that non-canonical witness encodings are rejected

#### Automated Tests (Phase 1)

1. `make check` — all unit tests pass
2. Functional tests — staker successfully generates blocks under new rules
3. `src/test/pos_tests.cpp` — PoS-specific tests with all four kernel types