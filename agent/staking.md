# Staking Flow Analysis

## Files Involved

- `src/node/miner.cpp` — PoSMiner, SignBlock, CreateNewBlock
- `src/wallet/staking.cpp` — CreateCoinStake, SelectCoinsForStaking
- `src/validation.cpp` — CheckBlockSignature
- `src/pos.cpp` — CheckProofOfStake

## Staking Flow

### 1. Destination Resolution (PoSMiner, miner.cpp:690-725)

1. Look up label `"SignKey"` with `AddressPurpose::SIGNKEY` from address book via `ForEachAddrBookEntry` (lines 696-704)
2. Self-healing guard (lines 706-709): if a stored `dest` exists but is neither `CNoDestination` nor `PKHash`, delete and reset (handles corrupted/legacy entries)
3. If not found, call `GetNewDestination(OutputType::LEGACY, label)` to create a new P2PKH address and set `AddressPurpose::SIGNKEY` (lines 711-718)
4. `dest` is a `CTxDestination` (P2PKH address), only used to create the block template via `CreateNewBlock(GetScriptForDestination(dest), ...)`. The actual reward destination is determined by the kernel type.

### 2. Block Assembly (CreateNewBlock, miner.cpp:140)

- Sets PoS difficulty via `GetNextTargetRequired(..., true)` (line 203)
- Calls `wallet::CreateCoinStake(*pwallet, pblock->nBits, 1, txCoinStake, nFees, destination)` at line 244
- If coinstake found, inserts it as `vtx[1]` and sets coinbase to empty (lines 247-249)

### 3. Coin Selection and Kernel Finding (CreateCoinStake, staking.cpp:252-415)

1. Select UTXOs meeting depth/amount criteria via `SelectCoinsForStaking`
2. For each UTXO, scan backward in time to find a kernel (age * value ≥ target)
3. When kernel found, proceed to output construction

### 4. Kernel Type Handling (staking.cpp:328-387)

`CreateCoinStake` accepts only four kernel types — anything else `break`s out of the search (line 329-332):

| Type | Script | Can stake? | Signature verified? |
|---|---|---|---|
| **PUBKEY** | `<pubkey> OP_CHECKSIG` | ✅ | ✅ Yes |
| **PUBKEYHASH** | `OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG` | ✅ | ✅ Yes |
| **WITNESS_V0_KEYHASH** | `OP_0 <20-byte-hash>` | ✅ | ❌ No (trivial pass) |
| **WITNESS_V1_TAPROOT** | `OP_1 <32-byte-key>` | ✅ **Verified on regtest** | ❌ No (trivial pass) |
| **SCRIPTHASH (P2SH)** | `OP_HASH160 <hash> OP_EQUAL` | ❌ | — |
| **WITNESS_V0_SCRIPTHASH** | `OP_0 <32-byte-hash>` | ❌ | — |
| **MULTISIG / NULL_DATA / NONSTANDARD** | — | ❌ | — |

The two Witness types can stake, but because `VerifySignature` passes `nullptr` for the witness (sign.cpp:734) and `SCRIPT_VERIFY_NONE` is used, their coinstake signature is never actually checked — security relies entirely on the kernel hash (stake modifier + prevout + timestamp). Only PUBKEY and PUBKEYHASH kernels get a real CHECKSIG verification.

#### After June 2026 cleanup — unified flow

The `bMinterKey` flag and P2PK intermediate output have been **removed**. Every kernel type follows the same structure:

1. **Single SignKey pubkey lookup** (before if/else chain): Get the block-signing pubkey from the `destination` parameter. All kernel types use the same key for the carrier.
2. **OP_RETURN carrier** built directly from the SignKey pubkey: `OP_RETURN <pubkey> <timestamp>`. No more Solver round-trip.
3. **Reward preserves the kernel's native script type**:
   - PUBKEY kernel: `scriptPubKeyOut = scriptPubKeyKernel` (P2PK reward)
   - PUBKEYHASH kernel: `scriptPubKeyOut` built from the kernel's hash160 → P2PKH reward
   - WITNESS_V0_KEYHASH / WITNESS_V1_TAPROOT kernel: `scriptPubKeyOut = scriptPubKeyKernel` — reward is P2WPKH or P2TR

### 5. Output Layout (unified)

```
vout[0]: empty (marker)
vout[1]: OP_RETURN <pubkey> <timestamp>  ← block-signing key carrier (non-spendable)
vout[2]: <native script type>            ← reward (P2PK/P2PKH/P2WPKH/P2TR per kernel)
vout[3]: <same type> split reward        ← if nCredit ≥ 1000 COIN
vout[4]: devfund                         ← if enabled
```

### 6. Signing (staking.cpp:488-513)

- **Legacy**: Uses `SignSignature(*GetLegacyScriptPubKeyMan(), ...)` to sign each coinstake input (line 494)
- **Descriptor**: Uses `wallet.SignTransaction()` which signs via descriptor providers (line 511). The wallet saves `txNew.nTime` before signing and restores it after (lines 510-512), because `SignTransaction` zeroes `nTime` for v2 transactions — see `agent/SegWitTxv2Coinstake.md` for the txid-collision implications and the OP_RETURN carrier fix.

### 7. Block Signing (SignBlock, miner.cpp:654-696)

After the OP_RETURN carrier fix, `vout[1]` is no longer P2PK — it is `OP_RETURN <pubkey> <timestamp>`. `SignBlock` handles both cases:

1. Reads vout[1] (or vout[0] for PoW) from the coinstake (line 657)
2. Checks `Solver(scriptPubKey, vSolutions)` — accepts either:
   - `TxoutType::PUBKEY` (fallback/PoW) — extracts pubkey directly from script
   - `TxoutType::NULL_DATA` (OP_RETURN carrier) — parses 2 GetOps, extracts pubkey from first push
3. Both paths extract a `CPubKey` and then sign via:
   - **Legacy path**: `GetLegacyScriptPubKeyMan()->GetKey(...)` → `key.Sign()` (lines 681-688)
   - **Descriptor path**: `PKHash(pubkey)` → `keystore.SignBlockHash()` (lines 691-694)

### 8. Validation (CheckBlockSignature, validation.cpp:3860-3897)

For PoS blocks:
- Checks vout[1] scriptPubKey type (line 3870)
- **PUBKEY** (lines 3872-3875): Verifies block signature against the pubkey in the script
- **NULL_DATA (OP_RETURN)** (lines 3876-3893): Extracts pubkey from OP_RETURN and verifies signature
- Other types: Rejected (explicit type guard; returns `false` at line 3896)

### 9. Proof of Stake Verification (CheckProofOfStake, pos.cpp)

- Verifies the kernel UTXO meets age/value requirements
- Calls `VerifySignature(coinPrev, txin.prevout.hash, tx, 0, SCRIPT_VERIFY_NONE)` at line 157
- Ensures coinstake input is properly signed

#### Why SCRIPT_VERIFY_NONE

`VerifySignature` (sign.cpp:718) passes `nullptr` for the witness parameter:

```cpp
return VerifyScript(txin.scriptSig, txout.scriptPubKey, nullptr, flags, checker);
```

Inside `VerifyScript` (interpreter.cpp:1973):
- Line 2006: `if (flags & SCRIPT_VERIFY_WITNESS)` — if **not** set, witness programs are **completely skipped**
- Line 2023: `if (flags & SCRIPT_VERIFY_P2SH)` — if **not** set, P2SH redeem script execution is **skipped**

| Kernel type | scriptPubKey | With SCRIPT_VERIFY_NONE | With P2SH\|WITNESS |
|---|---|---|---|
| **P2PK** | `<pubkey> OP_CHECKSIG` | CHECKSIG verifies signature (proper) | CHECKSIG verifies signature (same) |
| **P2PKH** | `OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG` | CHECKSIG verifies signature (proper) | CHECKSIG verifies signature (same) |
| **P2WPKH** | `OP_0 <hash160>` | Executed as plain script: push 0, push hash — stack top truthy → **passes, no sig check** | Witness program detected → `VerifyWitnessProgram` with empty witness → expects 2 items → **fails** |
| **P2SH** | `OP_HASH160 <hash> OP_EQUAL` | Just checks hash match — **passes, no redeem script** | Requires valid redeem script in scriptSig → coinstake doesn't have one → **fails** |
| **P2TR** | `OP_1 <32-byte-key>` | Executed as plain script: push 1, push key — stack top truthy → **passes, no sig check** | Witness v1 program → empty witness → **fails** |

**Security implication**: P2WPKH / P2SH / P2TR kernels have **no actual signature verification** with `SCRIPT_VERIFY_NONE`. The scriptPubKey executes as a plain script and always succeeds. Security for these kernel types relies entirely on the kernel hash computation. Only P2PK and P2PKH kernels are properly verified through CHECKSIG.

**Cross-codebase comparison:**

| Codebase | `CheckProofOfStake` signature verification | P2WPKH kernel sig verified? | P2TR kernel sig verified? |
|---|---|---|---|
| **Blackmore284** | `VerifySignature(..., SCRIPT_VERIFY_NONE)` → `VerifyScript(..., nullptr, 0, ...)` | ❌ No | ❌ No |
| **Qtum** | `VerifySignature(..., SCRIPT_VERIFY_NONE)` → `VerifyScript(..., NULL, 0, ...)` | ❌ No | ❌ No |
| **Peercoin** | `VerifyScript(..., &witness, SCRIPT_VERIFY_P2SH, ...)` | ✅ Yes | N/A (P2TR not supported) |

Peercoin fixed this by calling `VerifyScript` directly with the actual witness data and `SCRIPT_VERIFY_P2SH`. Applying the Peercoin fix to Blackmore284 is a **soft fork** (tightening rules: new nodes reject blocks with invalid signatures that old nodes accepted). It is safe if all existing P2WPKH/P2TR coinstakes have valid witness data.

**On-chain audit (June 2026, v28-SEGWIT branch):** Scanned all witness-kernel coinstakes from SegWit activation to tip on both networks. Every witness kernel has well-formed witness data (2-item `[DER-sig, compressed-pubkey]` for P2WPKH, no P2TR kernels found yet). Safe for soft fork.

| Network | SegWit activation block | Scanned to | Total coinstakes | Witness kernels | P2WPKH | P2TR | Malformed |
|---|---|---|---|---|---|---|---|
| **Testnet** | 2,070,000 | 2,852,570 | 782,417 | 61,196 | 61,196 | 0 | 0 |
| **Mainnet** | 5,805,000 | 5,928,105 | 123,106 | 3 | 3 | 0 | 0 |

**Taproot activation status** (as of June 2026): Mainnet BIP9 signaling to start with next release (late 2026). Testnet `locked_in` since block 2,850,000 — activates at block 2,865,000. Regtest `ALWAYS_ACTIVE`. The soft fork does not depend on Taproot activation — it fixes P2WPKH kernels (the only witness type on-chain). When Taproot activates, P2TR kernels will also be covered by the same fix.

**P2TR signing fix** (June 2026, v28-SEGWIT branch): `SCRIPT_VERIFY_TAPROOT` was missing from `STANDARD_SCRIPT_VERIFY_FLAGS` in `policy.h`, causing P2TR signing to silently fail. Without this flag, `VerifyWitnessProgram` (`interpreter.cpp:1920`) returns `set_success` without checking the witness — the signing test verification at `sign.cpp:572` treated P2TR as a no-op. This caused `::SignTransaction` to return `true` on the first (wrong) `ScriptPubKeyMan` instead of falling through to the correct P2TR descriptor. Fix: added `SCRIPT_VERIFY_TAPROOT` to `STANDARD_SCRIPT_VERIFY_FLAGS` (standard-only, not mandatory). Also added `SCRIPT_VERIFY_WITNESS` and `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` to `MANDATORY_SCRIPT_VERIFY_FLAGS`. Verified on regtest: P2TR regular sends and P2TR staking. See `agent/P2TRSigningFix.md`.

### 10. Why No TxIndex is Needed for Staking

Despite Blackcoin More enabling txindex by **default** (`DEFAULT_TXINDEX = true` in `index/txindex.h:10`), the staking hot path never touches it. The kernel hash requires no historical transaction data — only three sources, all available without txindex:

| Kernel hash parameter | Source |
|---|---|
| `nStakeModifier` | `CBlockIndex::nStakeModifier` (chain tip block index, in-memory) |
| `txPrev.nTime` | `Coin::nTime` (UTXO set LevelDB, or `blockFrom->nTime` fallback via `GetAncestor`) |
| `prevout.hash` + `prevout.n` | Already known (the UTXO being checked) |
| `nWeight (amount)` | `Coin::out.nValue` (UTXO set LevelDB) |
| `nTimeTx` | Current block timestamp |

Three data sources make this work, even for UTXOs from 2014:

#### Source 1: Wallet DB (`mapWallet`)

On startup, `WalletBatch::LoadWallet` in `walletdb.cpp:1160` loads **every** transaction the wallet has ever owned into `CWalletTx::mapWallet` — with **no age filter**. `LoadTxRecords` (line 1028) iterates all TX-keyed records via cursor, and `CWallet::LoadToWallet` (wallet.cpp:1202) inserts each into `mapWallet`. Each `CWalletTx` carries a full `CTransactionRef tx`, so `tx->vout[n].nValue` and `tx->vout[n].scriptPubKey` are always available in memory.

`AvailableCoinsForStaking` at `staking.cpp:110` iterates `mapWallet` directly. An explicit comment at `staking.cpp:310-312` documents the optimization:

> *"We use the cached transaction data in CWalletTx instead of hitting disk with g_txindex->FindTx. This reduces block creation time from ~100s to <100ms."*

#### Source 2: UTXO Set LevelDB

The `Coin` struct (`coins.h:34-105`) stores everything needed for the kernel hash:
- `CTxOut out` → `nValue`, `scriptPubKey`
- `uint32_t nHeight : 31` (line 47)
- `unsigned int nTime` (line 50)

Serialization writes all fields to LevelDB (`coins.h:76-82`). `CCoinsViewDB::GetCoin` in `txdb.cpp:68-70` reads them back with a single LevelDB read by outpoint key. Staking/verification uses this at:

```cpp
view.GetCoin(txin.prevout, coinPrev)                 // pos.cpp:142 (CheckProofOfStake)
view.GetCoin(prevout, coinPrev)                       // pos.cpp:178 (CheckKernel, cache miss)
view.GetCoin(prevout, coinPrev)                       // pos.cpp:218 (CacheKernel)
coinPrev.nTime                                       // pos.cpp:160, 198, 231
coinPrev.out.nValue                                  // pos.cpp:160, 198, 231
coinPrev.nHeight                                     // pos.cpp:147, 182, 222
(coinPrev.nTime ? coinPrev.nTime : blockFrom->nTime)  // pos.cpp:160, 198, 231 (fallback)
```

#### Source 3: In-Memory Block Index

`GetAncestor` in `chain.cpp:93-118` walks in-memory `pprev` and `pskip` pointers only — **never reads block files or txindex**. The `nTime` field is a plain `uint32_t` member of `CBlockIndex` (`chain.h:190`). Staking uses it as fallback at `pos.cpp:151, 187, 226`:

```cpp
CBlockIndex* blockFrom = pindexPrev->GetAncestor(coinPrev.nHeight);
// coinPrev.nTime ?: blockFrom->nTime
```

This works for any height, including UTXOs from block 1, because the entire block tree index is held in memory (in practice ~5GB for 800K+ blocks with all fields).

#### What TxIndex Is Actually Used For

TxIndex (`index/txindex.cpp`, `node/transaction.cpp:135`) is only needed for:
- **RPCs**: `getrawtransaction` on old/spent transactions
- **REST API**: Transaction lookups by txid
- **Backwards wallet lookup**: When a txid is requested that's no longer in `mapWallet`

None of these are on the staking hot path.

## Key Observations

1. **All kernel types use the same SignKey pubkey for the carrier** — the block-signing pubkey is always from the `destination` parameter (`"SignKey"` address), regardless of kernel type.
2. **`vout[1]` is always OP_RETURN carrier** (never P2PK). `vout[2]` is the reward with the kernel's native script type. The timestamp appended to the carrier is **not needed for consensus** — `CheckBlockSignature` reads only 2 `GetOp` calls (OP_RETURN + pubkey) and ignores the 3rd push. The timestamp exists purely as a defensive measure against the v2 txid collision edge case (orphan + retry from same UTXO). See `agent/SegWitTxv2Coinstake.md`.
3. **The `"SignKey"` address book entry** (with `AddressPurpose::SIGNKEY`) specifies the block-signing pubkey and is the sole carrier source. This is Blackcoin-specific — Peercoin uses a simple string label `"mintkey"` with no dedicated `AddressPurpose` enum value (Peercoin's `AddressPurpose` only has RECEIVE/SEND/REFUND). See `agent/P2PKMigration.md` for the full comparison.
4. **`bMinterKey` flag removed** — all kernel types produce the same carrier + reward layout. **Input combining is universal** — all four supported kernel types combine same-type UTXOs matching `scriptPubKeyKernel`. Additionally, P2PKH kernels sweep legacy P2PK reward UTXOs (pre-carrier era) via `scriptPubKeyP2PK` gated by `combineP2PK` at `staking.cpp:399`. The `nTimeSmart` guard at `staking.cpp:427` prevents `bad-txns-time-earlier-than-input` consensus violations.
5. **`COutPoint` comparison fix (June 29)** — the combining guard at `staking.cpp:414` was `pcoin.first->GetHash() != txNew.vin[0].prevout.hash`, which compared only the tx hash. This rejected **all** other outputs from the kernel's parent transaction — for example, an `optimizeutxoset` creating 42 identical P2WPKH outputs would only combine one with the kernel (others from the same tx were skipped). Fixed to compare the full outpoint: `COutPoint(pcoin.first->GetHash(), pcoin.second) != txNew.vin[0].prevout`. This affects all kernel types (P2PK, P2PKH, P2WPKH, P2TR).
6. **`SCRIPT_VERIFY_NONE`** — unchanged. `VerifySignature` still passes `nullptr` witness.
7. **`nTimeSmart` replaces `tx->nTime` in combining guard** (June 26 fix) — the input-combining loop at `staking.cpp:423-428` originally checked `pcoin.first->tx->nTime > txNew.nTime`. But for v2 transactions, `nTime` is never serialized and is always 0 on deserialization (transaction.h:233-236). The guard was a no-op. Fixed by using `pcoin.first->nTimeSmart` (block time for confirmed txs, matching `coin.nTime` from coins.cpp:129) instead.
8. **`updatedBlockTip()` is a pure wake signal** — it sets `m_new_block_arrived = true` and notifies `cv_new_block`, but no longer performs any MTP or timing math.
9. **Difficulty adjusts every block** (not at interval boundaries) — `CalculateNextTargetRequired` (`pow.cpp:54`) uses an exponential moving average formula: `bnNew *= ((nInterval-1)*nTargetSpacing + nActualSpacing*2) / ((nInterval+1)*nTargetSpacing)` where `nInterval = nTargetTimespan / nTargetSpacing = 960/64 = 15`. The `nActualSpacing` is `pindexLast->GetBlockTime() - pindexPrevPrev->GetBlockTime()` (time between last two PoS blocks). This runs for every block, so difficulty changes continuously, not in 15-block windows. `nTargetTimespan = 16 * 60` (16 minutes) is the smoothing window, not the adjustment interval.
10. **BIP94 is NOT active** on Blackcoin mainnet or testnet — `consensus.enforce_BIP94 = false` for both (`chainparams.cpp:116, 351`). Only regtest has it enabled (`chainparams.cpp:559`). BIP94 is a timewarp-attack mitigation that constrains the first block of each difficulty period. The code exists but is disabled everywhere except regtest. `MAX_TIMEWARP = 600` (10 minutes) is defined in `consensus.h:31` but unused on production networks.
11. **`optimizeutxoset` RPC** (`spend.cpp:351`) creates uniform UTXO outputs for staking. It selects all available coins (optionally filtered by `fromAddress`), calculates a fee, and creates as many outputs of the specified `amount` as possible via `CreateTransaction`. Change goes to the same address (`coin_control.destChange = dest`). Example: `optimizeutxoset <bech32-address> 250` creates N × 250 BLK P2WPKH outputs (if the address is P2WPKH) plus a change output. All outputs go to the same address, making them interchangeable for staking kernel selection. The output type is determined by the address format passed (Bech32 → P2WPKH, Bech32m → P2TR, legacy → P2PKH).

## 11. Staker Timing (PoSMiner Loop)

### 11.1 Design

Staking timing now uses a single-responsibility design entirely contained in `miner.cpp`. 

1. **Wake signal** from `CWallet::updatedBlockTip()` (`wallet/wallet.cpp`):
   ```cpp
   m_new_block_arrived.store(true);
   cv_new_block.notify_one();
   ```
   The wallet acts purely as a trigger. It does not calculate sleep boundaries.

2. **Core timing primitive** (`miner.cpp`): `MsUntilNextWindow()`
   Calculates the exact milliseconds until the next 16-second boundary, strictly advancing past the chain's Median Time Past (MTP).

3. **Timer guard** inside `BlockAssembler::CreateNewBlock` (`miner.cpp:239`):
   ```cpp
   int64_t nSearchTime = txCoinStake.nTime;
   if (nSearchTime > pwallet->m_last_coin_stake_search_time) {
       pwallet->m_last_coin_stake_search_time = nSearchTime;
   }
   ```
   This blocks re-entry within the same 16-second boundary even if the staker is spuriously awoken.

### 11.2 Sleep paths

`PoSMiner()` uses two sleep paths:

| Path | Sleep |
|---|---|
| Unsynced / Rescanning (`IsScanning()`) | 5000ms / 10000ms polling until synced/rescanned |
| Failed coinstake (`fPoSCancel`) | `std::max(MsUntilNextWindow(..., mtp), pos_timio)` |
| After successful block | `MsUntilNextWindow(..., mtp)` |

`pos_timio` acts as a CPU-throttling floor on failures:
```cpp
pos_timio = gArgs.GetIntArg("-staketimio", DEFAULT_STAKETIMIO) + 30 * sqrt(vCoins.size());
```

For full details, see `agent/SafetyBump.md`.

## The ExtractDestination Hack (addresstype.cpp:67-74)

Blackmore284 has a critical modification to `ExtractDestination` that bridges P2PK staking outputs to P2PKH address display:

```cpp
// Blackcoin: Reinterpret P2PK scripts as PKHash
if (!pubKey.IsValid())
    return false;
addressRet = PKHash(pubKey);  // Converts P2PK → PKHash for display
return true;
```

### Implications for P2PKH migration

**Current state (P2PK + hack)**:
- Staking output: P2PK script (`<pubkey> OP_CHECKSIG`)
- Wallet display: P2PKH address (via hack)
- Inconsistency: Script is P2PK, address is P2PKH

**After OP_RETURN carrier fix**:
- `vout[1]` = `OP_RETURN <pubkey> <timestamp>` (non-spendable carrier, ignored by wallet)
- `vout[2]` = native kernel type (P2PK/P2PKH/P2WPKH/P2TR) — naturally displayed by wallet
- `ExtractDestination` hack still needed for legacy P2PK reward UTXOs in the combining pool

### Consensus verification impact

**Current `CheckBlockSignature` (validation.cpp:3872-3875)**:
```cpp
if (whichType == TxoutType::PUBKEY) {
    std::vector<unsigned char>& vchPubKey = vSolutions[0];
    return CPubKey(vchPubKey).Verify(block.GetHash(), block.vchBlockSig);
}
```
This only accepts PUBKEY type. For P2PKH, it would need to recover the pubkey from a compact signature.

**Alternative: OP_RETURN mechanism (implemented)**:
Instead of P2PKH in `vout[1]`, the existing OP_RETURN carrier path (`validation.cpp:3876-3893`) extracts the pubkey from `OP_RETURN <pubkey> [message]` and verifies against it. This is consensus-valid today — no fork needed. Regtest verification (see `agent/SegWitTxv2Coinstake.md` for full proof) confirms OP_RETURN carrier with timestamp works for all kernel/wallet combinations.

## Peercoin Comparison

### Key Differences

| Component | Blackmore284 | Peercoin |
|-----------|--------------|----------|
| **ExtractDestination** | P2PK → PKHash for display | P2PK → PKHash (identical) |
| **SignBlock location** | `node/miner.cpp:654-696` | `validation.cpp:4960-4985` |
| **CheckBlockSignature** | Accepts PUBKEY + OP_RETURN | Accepts PUBKEY only (`validation.cpp:4994-5010`) |
| **CreateCoinStake location** | `wallet/staking.cpp:252-524` | `wallet/wallet.cpp:3570-3966` |
| **Staking label** | `"SignKey"` (with `AddressPurpose::SIGNKEY`) | `"mintkey"` |
| **WITNESS handling** | Carrier + native type for all kernel types | Uses `bMinterKey` for WITNESS kernels (unchanged) |

### CheckBlockSignature Differences

**Peercoin (`validation.cpp:4994-5010`)**:
```cpp
bool CheckBlockSignature(const CBlock& block)
{
    if (block.GetHash() == Params().GetConsensus().hashGenesisBlock)
        return block.vchBlockSig.empty();

    std::vector<valtype> vSolutions;
    const CTxOut& txout = block.IsProofOfStake()? block.vtx[1]->vout[1] : block.vtx[0]->vout[0];

    if (Solver(txout.scriptPubKey, vSolutions) != TxoutType::PUBKEY)
        return false;

    const valtype& vchPubKey = vSolutions[0];
    CPubKey key(vchPubKey);
    if (block.vchBlockSig.empty())
        return false;
    return key.Verify(block.GetHash(), block.vchBlockSig);
}
```

**Blackmore284 (`validation.cpp:3860-3897`)** adds OP_RETURN carrier support — reads 2 `GetOp`s, ignores extra pushes (the timestamp). Peercoin only supports PUBKEY type.

### CreateCoinStake Differences

- **Blackmore284** (after June 2026 cleanup): All kernel types produce native reward type (no P2PK conversion). OP_RETURN carrier always emitted from SignKey.
- **Peercoin**: Still converts PUBKEYHASH to P2PK; uses `bMinterKey` for WITNESS kernels (unchanged)
- Output split: Peercoin uses RFC28 security level calculation; Blackmore284 uses simple 1000 COIN threshold
- Fee handling: Peercoin calculates min fee; Blackmore284 coinstakes are free

### ExtractDestination

Both codebases have identical behavior for PUBKEY extraction (`P2PK → PKHash`). The Blackcoin comment explains the "why" but the code is the same.
