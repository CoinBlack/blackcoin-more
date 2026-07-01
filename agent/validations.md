# Blackcoin More v28 — Validation Architecture

All validation checks traced directly from the Blackcoin More v28 source code.

---

## 1. Transaction Validation (`CheckTransaction` in `src/consensus/tx_check.cpp`)

Context-free checks applied to every transaction.

| # | Check | Reject reason | Line |
|---|-------|---------------|------|
| 1 | `vin` is not empty | `bad-txns-vin-empty` | 14 |
| 2 | `vout` is not empty | `bad-txns-vout-empty` | 16 |
| 3 | Non-witness serialized size × `WITNESS_SCALE_FACTOR` ≤ `MAX_BLOCK_WEIGHT` | `bad-txns-oversize` | 19 |
| 4 | Empty outputs (`txout.IsEmpty()`) forbidden unless tx is coinbase or coinstake | `bad-txns-vout-empty` | 27 |
| 5 | Each `txout.nValue` ≥ 0 | `bad-txns-vout-negative` | 29 |
| 6 | Each `txout.nValue` ≤ `MAX_MONEY` (`int64_t::max()` in Blackcoin) | `bad-txns-vout-toolarge` | 31 |
| 7 | Cumulative sum of all output values ≤ `MAX_MONEY` | `bad-txns-txouttotal-toolarge` | 34 |
| 8 | No duplicate inputs (same `COutPoint`) | `bad-txns-inputs-duplicate` | 45 |
| 9 | If coinbase: `scriptSig` size must be 2–100 bytes | `bad-cb-length` | 51 |
| 10 | If non-coinbase: no input may have a null `prevout` | `bad-txns-prevout-null` | 57 |

---

## 2. Block Header Validation (`CheckBlockHeader` in `src/validation.cpp:3899`)

Context-free checks on the block header only.

| # | Check | Reject reason | Line |
|---|-------|---------------|------|
| 1 | `nVersion` ≥ 7 if ProtocolV2 is active | `bad-version` | 3902 |
| 2 | PoW hash (`GetPoWHash()` — scrypt) satisfies `nBits` target (PoW blocks only) | `high-hash` | 3906 |
| 3 | Block timestamp ≤ `FutureDrift(GetAdjustedTimeSeconds())` | `time-too-new` | 3911 |

**Blackcoin note:** `FutureDrift` returns `nTime + 15` seconds (ProtocolV2) or `nTime + 10 minutes` (pre-V2). In regtest during PoW phase it returns `nTime + 24 hours`.

**Blackcoin note:** `GetPoWHash()` uses scrypt (1024, 1, 1, 256). Since `nVersion > 6`, `GetHash()` uses SHA-256d while `GetPoWHash()` always uses scrypt.

---

## 3. Contextual Header Validation (`ContextualCheckBlockHeader` in `src/validation.cpp:4199`)

Contextual checks that depend on the previous block's `CBlockIndex`.

| # | Check | Reject reason | Line |
|---|-------|---------------|------|
| 1 | `nBits` == `GetNextTargetRequired(pindexPrev, fProofOfStake)` | `bad-diffbits` | 4208 |
| 2 | Fork depth < `nMaxReorganizationDepth` | `older-than-maxreorg-depth` | 4212 |
| 3 | If `nHeight > nLastPOWBlock`: block must be PoS (rejects PoW) | `reject-pow` | 4218 |
| 4 | If PoS block: `CheckStakeBlockTimestamp(nTimeBlock)` passes | `bad-pos-time` | 4222 |
| 5 | Hardened checkpoints pass | `bad-fork-hardened-checkpoint` | 4235 |
| 6 | Synchronized (rolling) checkpoint passes | `bad-fork-prior-to-synch-checkpoint` | 4242 |
| 7 | Block timestamp > `pindexPrev->GetMedianTimePast()` | `time-too-old` | 4246 |
| 8 | Block timestamp ≤ `now + MAX_FUTURE_BLOCK_TIME` | `time-too-new` | 4262 |

**Blackcoin note on MTP:** Since ProtocolV2, `GetMedianTimePast()` simply returns `GetBlockTime()` (the exact previous block timestamp), NOT the median of 11 blocks. This forces strictly monotonically increasing block times.

**Blackcoin note on timestamps:** `CheckStakeBlockTimestamp(nTimeBlock)` calls `CheckCoinStakeTimestamp(nTimeBlock, nTimeBlock)` which verifies `(nTimeTx & nStakeTimestampMask) == 0`. The mask is `0xf` (15), so PoS block timestamps must be divisible by 16.

**Blackcoin note on difficulty adjustment:** Difficulty is recalculated **every block** (not at interval boundaries) via `CalculateNextTargetRequired` (`pow.cpp:54`). The formula is an exponential moving average: `bnNew *= ((nInterval-1)*nTargetSpacing + nActualSpacing*2) / ((nInterval+1)*nTargetSpacing)` where `nInterval = nTargetTimespan / nTargetSpacing = 960/64 = 15` and `nActualSpacing` is the time between the last two PoS blocks. `nTargetTimespan = 16 * 60` (16 minutes) is the smoothing window, not a fixed adjustment interval. The EMA continuously adjusts difficulty at each block boundary.

**Blackcoin note on `PermittedDifficultyTransition`:** This function (`pow.cpp:101`) is a **no-op** for Blackcoin — it unconditionally returns `true`. Bitcoin uses it during header sync (PRESYNC phase) to prevent an adversary from claiming arbitrary difficulty jumps between consecutive headers (max 4x change). Blackcoin must disable it because PoW and PoS blocks have **separate difficulty tracks** (`GetLastBlockIndex` walks back to the last block of the same type), so consecutive headers routinely have dramatically different `nBits` values. A PoS block may have 1/100th the target of the preceding PoW block — a valid transition that Bitcoin's 4x check would reject. See `agent/review_blackcoin_more_2840.md` §12.7 for the full analysis of why this makes the PRESYNC phase effectively a no-op for Blackcoin.

**BIP94 status:** `consensus.enforce_BIP94 = false` on both mainnet (`chainparams.cpp:116`) and testnet4 (`chainparams.cpp:351`). Only regtest enables it (`chainparams.cpp:559`). BIP94 is a timewarp-attack mitigation that constrains the first block of each difficulty period's timestamp to be no earlier than `MAX_TIMEWARP = 600` seconds before the last block of the previous period (`consensus.h:31`). The code exists but is **not active** on production networks. The BIP94-related difficulty calculation at `pow.cpp:78-84` (using the first block of the period as baseline) only runs when `enforce_BIP94 = true`; otherwise the previous block's difficulty is used as baseline.

---

## 4. Block Validation (`CheckBlock` in `src/validation.cpp:3998`)

Context-free checks on the full block (header + transactions).

| # | Check | Reject reason | Line |
|---|-------|---------------|------|
| 1 | `CheckBlockHeader()` passes | *(propagated)* | 4007 |
| 2 | Merkle root matches AND no merkle tree malleability (CVE-2012-2459) | `bad-txnmrklroot` / `bad-txns-duplicate` | 4016 |
| 3 | Block not empty, tx count × `WITNESS_SCALE_FACTOR` ≤ `MAX_BLOCK_WEIGHT`, non-witness serialize size × `WITNESS_SCALE_FACTOR` ≤ `MAX_BLOCK_WEIGHT` | `bad-blk-length` | 4027 |
| 4 | `vtx[0]` is coinbase | `bad-cb-missing` | 4031 |
| 5 | No other tx is a coinbase | `bad-cb-multiple` | 4033 |
| 6 | Coinbase timestamp check: `block.GetBlockTime()` ≤ `FutureDrift(vtx[0]->nTime)` | `bad-cb-time` | 4038 |
| 7 | **PoS only:** Coinstake timestamp: `CheckCoinStakeTimestamp(block.nTime, vtx[1]->nTime)` | `bad-cs-time` | 4042 |
| 8 | **PoS only, no SegWit commitment:** coinbase must have exactly 1 output, and it must be empty | `bad-cb-not-empty` | 4049 |
| 9 | **PoS only, with SegWit commitment:** coinbase must have exactly 2 outputs, `vout[0]` empty, `vout[1].nValue == 0` | `bad-cb-not-empty` | 4052 |
| 10 | **PoS only:** `vtx[1]` is coinstake | `bad-cs-missing` | 4057 |
| 11 | **PoS only:** No other tx is a coinstake | `bad-cs-multiple` | 4059 |
| 12 | `CheckBlockSignature()` passes (see below) | `bad-blk-signature` | 4065 |
| 13 | `CheckTransaction()` passes for every tx | *(propagated)* | 4072 |
| 14 | Each tx timestamp: `block.GetBlockTime()` ≥ `tx.nTime` (if `tx.nTime` is set) | `bad-tx-time` | 4081 |
| 15 | Legacy SigOps × `WITNESS_SCALE_FACTOR` ≤ `MAX_BLOCK_SIGOPS_COST` | `bad-blk-sigops` | 4089 |

### `CheckBlockSignature` (`src/validation.cpp:3860`)

| Block type | Rule |
|---|---|
| **PoW** | `vchBlockSig` must be **empty** (PoW blocks have no signature) |
| **PoS with P2PK `vout[1]`** | Extract pubkey from `vout[1]` P2PK script, verify `CPubKey.Verify(block.GetHash(), vchBlockSig)` |
| **PoS with OP_RETURN `vout[1]`** | Parse the first push after `OP_RETURN`, verify it is a valid compressed/uncompressed pubkey, verify `CPubKey.Verify(block.GetHash(), vchBlockSig)` |

---

## 5. Contextual Block Validation (`ContextualCheckBlock` in `src/validation.cpp:4275`)

Contextual checks before connecting to the UTXO set.

| # | Check | Reject reason | Line |
|---|-------|---------------|------|
| 1 | All transactions are final: `IsFinalTx(tx, nHeight, nLockTimeCutoff)` | `bad-txns-nonfinal` | 4292 |
| 2 | Coinbase `scriptSig` starts with serialized block height (BIP34 — always active in Blackcoin) | `bad-cb-height` | 4301 |
| 3 | `CheckWitnessMalleation()`: if SegWit active and witness commitment exists, coinbase witness stack must be exactly 1 item of 32 bytes, and the witness Merkle root must match the commitment | `bad-witness-nonce-size` / `bad-witness-merkle-match` | 4315 |
| 4 | `CheckWitnessMalleation()`: if SegWit NOT active (or no commitment), no transaction may contain witness data | `unexpected-witness` | 4315 |
| 5 | Full block weight (`GetBlockWeight()`) ≤ `MAX_BLOCK_WEIGHT` | `bad-blk-weight` | 4325 |

**Blackcoin note on MTP cutoff:** `nLockTimeCutoff` uses `pindexPrev->GetMedianTimePast()` when BIP113 (CSV deployment) is active. Since Blackcoin's MTP just returns the previous block's timestamp, lock time is evaluated against the exact previous block time.

---

## 6. Coinbase vs. Coinstake Structural Definitions

Defined in `src/primitives/transaction.h`.

| Property | `IsCoinBase()` | `IsCoinStake()` |
|---|---|---|
| Input count | Exactly 1 | ≥ 1 |
| `vin[0].prevout` | Must be null (all zeros) | Must NOT be null (real UTXO) |
| Output count | Any | ≥ 2 |
| `vout[0]` | No constraint | Must be empty (0-value marker) |

---

## 7. Input Validation (`Consensus::CheckTxInputs` in `src/consensus/tx_verify.cpp:165`)

Called from `ConnectBlock` for every non-coinbase transaction.

| # | Check | Reject reason | Line |
|---|-------|---------------|------|
| 1 | All inputs exist and are unspent in the UTXO set | `bad-txns-inputs-missingorspent` | 168 |
| 2 | If prev output is coinbase or coinstake: must be matured (`nSpendHeight - coin.nHeight ≥ nCoinbaseMaturity`). Maturity is 500 blocks on mainnet, 10 on testnet | `bad-txns-premature-spend-of-coinbase` | 185 |
| 3 | Each input's coin timestamp ≤ transaction timestamp (`coin.nTime ≤ nTimeTx`) | `bad-txns-time-earlier-than-input` | 191 |
| 4 | All input values are in valid `MoneyRange` | `bad-txns-inputvalues-outofrange` | 196 |
| 5 | For non-coinstake: sum of inputs ≥ sum of outputs | `bad-txns-in-belowout` | 204 |
| 6 | Fee is in valid `MoneyRange` | `bad-txns-fee-outofrange` | 211 |
| 7 | **Blackcoin (ProtocolV3.1):** Fee ≥ `GetMinFee(tx)` (minimum relay fee enforced at consensus) | `bad-txns-fee-not-enough` | 216 |

**Blackcoin note:** For coinstake transactions, the sum of outputs is allowed to exceed inputs (the difference is the block reward). Fee accounting is skipped for coinstakes.

**Blackcoin note on `nTimeTx`:** If `tx.nTime == 0` and `tx.version >= 2`, `nTimeTx` defaults to `GetAdjustedTimeSeconds()`.

---

## 8. Execution Validation (`ConnectBlock` in `src/validation.cpp:2460`)

The final step: applying the block to the UTXO set.

| # | Check | Reject reason | Line |
|---|-------|---------------|------|
| 1 | `CheckBlock()` passes (re-run inside ConnectBlock) | *(propagated)* | 2484 |
| 2 | **PoS (ProtocolV3):** `CheckProofOfStake()` passes (see below) | *(propagated)* | 2505 |
| 3 | BIP30: no existing unspent outputs with same txid | `bad-txns-BIP30` | 2561 |
| 4 | `Consensus::CheckTxInputs()` passes for every non-coinbase tx | *(propagated)* | 2610 |
| 5 | Accumulated fees remain in `MoneyRange` | `bad-txns-accumulated-fee-outofrange` | 2620 |
| 6 | BIP68 sequence locks satisfied | `bad-txns-nonfinal` | 2631 |
| 7 | `GetTransactionSigOpCost()` (legacy + P2SH + witness) ≤ `MAX_BLOCK_SIGOPS_COST` | `bad-blk-sigops` | 2642 |
| 8 | **PoW:** `vtx[0]->GetValueOut()` ≤ `nFees + GetBlockSubsidy(false)` | `bad-cb-amount` | 2684 |
| 9 | **PoS (ProtocolV3):** coinstake net reward ≤ `nFees + GetBlockSubsidy(true)` | `bad-cs-amount` | 2692 |
| 10 | Script verification passes (`CheckInputScripts`) for all non-coinbase tx | `block-validation-failed` | 2657 |
| 11 | Stake modifier computed: `ComputeStakeModifier(pindexPrev, kernel_or_blockhash)` | *(no reject — stored)* | 2711 |

**Blackcoin note on block subsidy:**
- PoW: `GetProofOfWorkSubsidy()` = 10,000 BLK
- PoS: `GetProofOfStakeSubsidy()` = 1.5 BLK

**Blackcoin note on coinstake reward math:** The coinstake net reward is calculated as `tx.GetValueOut() - view.GetValueIn(tx)` (output minus input), which is the profit extracted by the staker. This must not exceed `nFees + 1.5 BLK`.

---

## 9. Proof-of-Stake Kernel Validation (`CheckProofOfStake` in `src/pos.cpp:130`)

Called from `ConnectBlock`. Requires the UTXO set.

| # | Check | Reject reason | Line |
|---|-------|---------------|------|
| 1 | Transaction is a coinstake (`IsCoinStake()`) | *(logged error)* | 132 |
| 2 | Kernel input (`vin[0].prevout`) exists in `CCoinsView` | `stake-prevout-not-exist` | 142 |
| 3 | Kernel coin age: `(pindexPrev.nHeight + 1) - coin.nHeight ≥ nCoinbaseMaturity` | `stake-prevout-not-mature` | 147 |
| 4 | Block at `coin.nHeight` can be loaded from block index | `stake-prevout-not-loaded` | 152 |
| 5 | `VerifySignature()` on the coinstake input passes | `stake-verify-signature-failed` | 157 |
| 6 | `CheckStakeKernelHash()`: `hash(nStakeModifier + prevTx.nTime + prevout.hash + prevout.n + nTimeTx) < bnTarget × nWeight` | `stake-check-kernel-failed` | 160 |

**Blackcoin note on kernel hash:** The hash is weight-proportional — `bnTarget` is multiplied by the coin value (`nValueIn`), so the chance of finding a valid kernel is directly proportional to the amount of coins staked.

---

## 10. Identified Overlaps & Redundant Validations

To optimize consensus validation, the codebase contains several overlaps where rules are checked multiple times (usually first in a context-free fast sanity check, then later in a context-dependent verification check).

### A. Size & Weight Limits
1.  **Transaction Level (`CheckTransaction`):** Checks non-witness serialized transaction size × `WITNESS_SCALE_FACTOR` ≤ `MAX_BLOCK_WEIGHT`.
2.  **Block Level (`CheckBlock`):** Checks block transaction count × `WITNESS_SCALE_FACTOR` ≤ `MAX_BLOCK_WEIGHT` and block non-witness serialized size × `WITNESS_SCALE_FACTOR` ≤ `MAX_BLOCK_WEIGHT`.
3.  **Contextual Block Level (`ContextualCheckBlock`):** Checks actual total block weight (`GetBlockWeight(block)`) including witness data against `MAX_BLOCK_WEIGHT`.

### B. SigOps Validation
1.  **Context-Free (`CheckBlock`):** Counts and checks only legacy (non-script) SigOps of all transactions against `MAX_BLOCK_SIGOPS_COST`.
2.  **Contextual (`ConnectBlock`):** Recalculates legacy SigOps plus P2SH and witness SigOps using UTXO input data (`GetTransactionSigOpCost`), enforcing the limit again.

### C. Merkle Tree Malleability
1.  **Transaction Tree (`CheckMerkleRoot`):** Checks for Merkle tree malleability (repeating transaction hashes that resolve to the same root).
2.  **Witness Tree (`CheckWitnessMalleation`):** Performs witness-tree malleability checks. As noted in the source comments, this second check is mathematically redundant since malleability is already blocked at the transaction level.

### D. Time & Sequence Constraints
1.  **Future Drift Check:** Block header time must be ≤ `FutureDrift(now)`. Coinbase time must be ≤ `FutureDrift(block.nTime)`.
2.  **Transaction Pacing:** In `CheckBlock`, every transaction's timestamp must satisfy `block.nTime >= tx.nTime` (if `tx.nTime` is set).
3.  **Input Pacing (`CheckTxInputs`):** Every spent UTXO's birth block/transaction timestamp must be ≤ spending transaction's timestamp (`coin.nTime <= nTimeTx`).
4.  **Coinstake Alignment:** Proof-of-Stake blocks enforce that the block timestamp and the coinstake transaction timestamp (`vtx[1]->nTime`) match exactly.

