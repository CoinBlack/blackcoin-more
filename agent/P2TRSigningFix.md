# P2TR Signing Fix — Missing SCRIPT_VERIFY_TAPROOT

**Branch:** v28-SEGWIT
**Date:** June 26, 2026
**Status:** COMPLETED. Fixed and verified on regtest. Subsequent fixes: (a) input combining enabled for all kernel types (same-type via `scriptPubKeyKernel`, P2PKH additionally sweeps old P2PK via `combineP2PK`), (b) `staking.cpp:427` `nTimeSmart` replaces `tx->nTime` (always 0 for v2 txs, making the timing guard a no-op — see `transaction.h:233-236`).

---

## 1. The Bug

P2TR (Taproot) transactions — both regular sends and staking coinstakes — failed with empty witnesses. The debug log showed:

```
BUG! PLEASE REPORT THIS! CheckInputScripts failed against latest-block but not STANDARD flags
... non-mandatory-script-verify-flag (Witness program was passed an empty witness)
```

The transaction passed mempool's `STANDARD_SCRIPT_VERIFY_FLAGS` check (because P2TR was a no-op without `SCRIPT_VERIFY_TAPROOT`) but failed the consensus check (which included `SCRIPT_VERIFY_TAPROOT` on regtest where Taproot is `ALWAYS_ACTIVE`).

## 2. Root Cause

`SCRIPT_VERIFY_TAPROOT` was missing from both `MANDATORY_SCRIPT_VERIFY_FLAGS` and `STANDARD_SCRIPT_VERIFY_FLAGS` in `src/policy/policy.h`. This was an oversight when porting the Bitcoin Core 28.4.0 Taproot code — the signing/sighash/descriptor code was ported correctly, but the policy flags were not updated.

### How the missing flag caused empty witnesses

The signing flow at `sign.cpp:572`:
```cpp
sigdata.complete = solved && VerifyScript(sigdata.scriptSig, fromPubKey, &sigdata.scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, creator.Checker());
```

Inside `VerifyWitnessProgram` (`interpreter.cpp:1920`):
```cpp
if (witversion == 1) {
    if (!(flags & SCRIPT_VERIFY_TAPROOT)) return set_success(serror);
}
```

Without `SCRIPT_VERIFY_TAPROOT` in `STANDARD_SCRIPT_VERIFY_FLAGS`:
1. `VerifyScript` returns `true` without checking the witness — P2TR is anyone-can-spend
2. `sigdata.complete = solved && true` — empty witness passes
3. `::SignTransaction` returns true, `input_errors` empty
4. First `ScriptPubKeyMan` (no P2TR keys) falsely reports success
5. `CWallet::SignTransaction` exits early — never tries the correct P2TR descriptor
6. Transaction committed with empty witness; consensus check rejects it

### Why P2WPKH worked but P2TR didn't

`SCRIPT_VERIFY_WITNESS` was already in `STANDARD_SCRIPT_VERIFY_FLAGS` — so P2WPKH signing was properly verified. Only `SCRIPT_VERIFY_TAPROOT` was missing, affecting P2TR exclusively.

## 3. The Fix

In `src/policy/policy.h`:

**Added to `MANDATORY_SCRIPT_VERIFY_FLAGS`** (bannable — already active on all networks via existing deployments):
- `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` (BIP112, active via `DEPLOYMENT_CSV`)
- `SCRIPT_VERIFY_WITNESS` (BIP141, active via `DEPLOYMENT_SEGWIT`)

**Added to `STANDARD_SCRIPT_VERIFY_FLAGS` only** (not mandatory — not yet consensus on mainnet):
- `SCRIPT_VERIFY_TAPROOT` (BIP341/342, active on regtest/testnet but not mainnet)

### Why TAPROOT is standard-only, not mandatory

On mainnet, Taproot activates via BIP9 (signaling starts with next release, late 2026). Before activation:
- **Consensus**: P2TR is anyone-can-spend (`GetBlockScriptFlags` does NOT add `SCRIPT_VERIFY_TAPROOT` until deployment activates)
- **Mempool (STANDARD)**: P2TR is verified — invalid P2TR transactions are rejected from mempool
- **DoS (MANDATORY)**: Peers sending invalid P2TR are NOT banned — treated as `TX_NOT_STANDARD`, not `TX_CONSENSUS`

If `TAPROOT` were in `MANDATORY`, peers would be **banned** for sending invalid P2TR transactions on mainnet where Taproot isn't consensus yet. The two-tier check at `validation.cpp:2230-2242` handles this:

```cpp
if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
    // Re-check without non-mandatory flags
    CScriptCheck check2(..., flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, ...);
    if (check2())
        return state.Invalid(TxValidationResult::TX_NOT_STANDARD, ...);
}
// Falls through to TX_CONSENSUS (bannable) only if mandatory flags fail
```

With `TAPROOT` in STANDARD-only: the re-check (without TAPROOT) passes → returns `TX_NOT_STANDARD` → peer NOT banned.

After mainnet Taproot activation, `TAPROOT` can be moved to `MANDATORY` in a follow-up release to enable banning.

## 4. Verification on Regtest (June 25, 2026)

### P2TR regular send
- Transaction `efc91de8da...` (height ~1087)
- Input: P2TR UTXO from `d995670e61:0`
- Witness: 64-byte Schnorr signature (key-path spend)
- Mempool: accepted, no "BUG!" error
- Output: P2TR (`5120c1cafa...`)

### P2WPKH staking
- Block `f102b54d...` (height 1088)
- Kernel: P2WPKH (`00148ad2b7...`)
- Coinstake witness: `[DER sig + 01, compressed pubkey]` — 2 items
- Block: connected, `state=Valid`

### P2TR staking
- Block `aaffff37...` (height 1186, 12 confirmations)
- Kernel: P2TR (`5120a67448...`) — input from `38a942ae6a:0`
- Coinstake witness: 65-byte Schnorr signature (`aa12fe88...01`, key-path spend with SIGHASH_ALL)
- OP_RETURN carrier: `034e19fa...` + timestamp `1782421696` in vout[1]
- Reward outputs: P2TR (`5120a67448...`) in vout[2] and vout[3], 5000.75 BLK each
- Block signature: ECDSA `304402201dfa...` (via OP_RETURN carrier pubkey)
- Block: connected, 12 confirmations

Full block JSON:
```json
{
  "hash": "aaffff370c4fbd40d4500c264cb68eb6252af7ace24f05afc4f2af71aa68252b",
  "confirmations": 12,
  "height": 1186,
  "flags": "proof-of-stake",
  "tx": [
    {
      "txid": "5138948bae9995024c732819efeb4756124c02c390e46671414133cc7eedd0c1",
      "vin": [{ "coinbase": "02a2040101" }]
    },
    {
      "txid": "ac9040b46ac7337be633b9fec21b43c366d192deca744cfcde94ed127969ba90",
      "vin": [{
        "txid": "38a942ae6a790fd3e7d93ba0205e00534897abbd8303cfd7c53827ba95617b12",
        "vout": 0,
        "scriptSig": { "asm": "", "hex": "" },
        "txinwitness": ["aa12fe88498f965bb583ee6d90df7b738227b1dad1dcf532ab0f16f8ac78d806e955c7ce85959dbdb57b4456a9af876584fc8ad7fd31be4af54298f3c1c2ee5f01"]
      }],
      "vout": [
        { "value": 0.00000000, "n": 0 },
        { "value": 0.00000000, "n": 1, "scriptPubKey": {
            "asm": "OP_RETURN 034e19fa598e0e2d884a46a4021b8254ac47935dcc20663cead7428118152f238d 1782421696",
            "type": "nulldata"
        }},
        { "value": 5000.75000000, "n": 2, "scriptPubKey": {
            "asm": "1 a6744854b6e3ad5c1ca32eda5ea407d7b6e543cc0be44e8c66540c467a2c37ae",
            "type": "witness_v1_taproot",
            "address": "blrt1p5e6ys49kuwk4c89r9md9afq867mw2s7vp0jyarrx2sxyv73vx7hqr5rpa2"
        }},
        { "value": 5000.75000000, "n": 3, "scriptPubKey": {
            "asm": "1 a6744854b6e3ad5c1ca32eda5ea407d7b6e543cc0be44e8c66540c467a2c37ae",
            "type": "witness_v1_taproot",
            "address": "blrt1p5e6ys49kuwk4c89r9md9afq867mw2s7vp0jyarrx2sxyv73vx7hqr5rpa2"
        }}
      ],
      "reward": 1.50000000
    }
  ],
  "signature": "304402201dfaff3441f308065a55065b28bba07c72133832ac4323ec31b15bb87cb8870f022058e92d67c62f2c3066732b3da16fade25823be191686b0c01f3dfa3785b0d050"
}
```

### Legacy staking (regression check)
- P2PK and P2PKH staking: unaffected (uses `SCRIPT_VERIFY_NONE` in `CheckProofOfStake`, bypassing all flags)
- Block signing: unaffected (`CheckBlockSignature` doesn't use script flags — directly calls `CPubKey::Verify`)

## 5. Files Changed

| File | Change |
|---|---|
| `src/policy/policy.h` | Added `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY`, `SCRIPT_VERIFY_WITNESS` to `MANDATORY_SCRIPT_VERIFY_FLAGS` (SegWit merge gap); added `SCRIPT_VERIFY_TAPROOT` to `STANDARD_SCRIPT_VERIFY_FLAGS` |
| `src/wallet/staking.cpp` | OP_RETURN carrier implementation + `SignTransaction` return-value check (pre-existing, not part of this fix) |
| `src/node/miner.cpp` | `SignBlock` OP_RETURN carrier support (pre-existing, not part of this fix) |

## 6. Comparison with Bitcoin Core 28.4.0

| Flag | Blackmore MANDATORY | Bitcoin Core MANDATORY | Blackmore STANDARD | Bitcoin Core STANDARD |
|---|---|---|---|---|
| `SCRIPT_VERIFY_DERKEY` | ✅ | ❌ (doesn't exist) | ✅ (via MANDATORY) | ❌ |
| `SCRIPT_VERIFY_LOW_S` | ✅ | ❌ | ✅ (via MANDATORY) | ✅ |
| `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` | ✅ | ✅ | ✅ (via MANDATORY) | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_WITNESS` | ✅ | ✅ | ✅ (via MANDATORY) | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_TAPROOT` | ❌ | ✅ | ✅ | ✅ (via MANDATORY) |

Key differences:
- `DERKEY`: Blackcoin-specific flag (bit 31), doesn't exist in Bitcoin Core
- `LOW_S`: Blackcoin enforces as mandatory (bannable), Bitcoin Core as standard-only (non-bannable)
- `CHECKSEQUENCEVERIFY` / `WITNESS`: Blackmore matches Bitcoin Core — both in MANDATORY. v28-CORE pre-SegWit base was missing them; the SegWit merge should have added them but didn't, caught and fixed during the P2TR signing fix audit.
- `TAPROOT`: Blackmore puts in STANDARD-only (not bannable until mainnet activates via BIP9), Bitcoin Core puts in MANDATORY (always bannable, since Taproot is always active on Bitcoin)

## 7. Debug Findings (Summary)

Before the fix, `P2TR_DEBUG` logging was temporarily added to `sign.cpp`, `scriptpubkeyman.cpp`, and `wallet.cpp`. The logs revealed:
1. 8 `ScriptPubKeyMan`s in the descriptor wallet
2. First 7 did NOT have the P2TR script in `m_map_script_pub_keys`
3. 8th spk_man found the P2TR script: `tr_trees=1 keys=1 pubkeys=1`
4. `SignTaproot` succeeded correctly (`CreateSchnorrSig`, 64/65-byte sigs)
5. Before the fix, the first spk_man falsely "succeeded" because the no-op P2TR verification masked the empty witness

All debug logging removed. The only remaining change is `policy.h`.

## 8. libsecp256k1 Build Configuration

The bundled libsecp256k1 is configured at `configure.ac:1714`:

```
--disable-shared --with-pic --enable-benchmark=no --enable-module-recovery --disable-module-ecdh
```

| Module | Enabled | Purpose | Used by Blackcoin |
|---|---|---|---|
| `recovery` | ✅ Yes | ECDSA pubkey recovery (compact signatures) | ✅ `CKey::SignCompact()`, `CPubKey::RecoverCompact()` — used by `signmessagewithprivkey`/`verifymessage` RPC, and documented as a future P2PKH block-signing option (`P2PKMigration.md` §2.1) |
| `extrakeys` | ✅ Yes | Extra key functions (x-only pubkeys) | ✅ Required by `schnorrsig` module |
| `schnorrsig` | ✅ Yes | BIP340 Schnorr signatures | ✅ P2TR signing (`CKey::SignSchnorr`), P2TR staking on regtest/testnet |
| `ellswift` | ✅ Yes | ElligatorSwift-encoded public keys + ECDH | ✅ BIP324 P2P v2 encrypted transport (`ComputeBIP324ECDHSecret` uses `secp256k1_ellswift_xdh`) |
| `ecdh` | ❌ No | Standard ECDH (`secp256k1_ecdh`) | ❌ Not used — BIP324 uses `ellswift_xdh` instead, which is self-contained in the ellswift module and does not depend on `module_ecdh` |

### Why `module_ecdh` is disabled

Blackcoin (inherited from Bitcoin Core) uses the **ellswift module** for BIP324 P2P v2 encrypted transport. The ellswift module provides `secp256k1_ellswift_xdh()` — a separate ECDH implementation that works with ElligatorSwift-encoded public keys and includes its own hash function (`secp256k1_ellswift_xdh_hash_function_bip324`). It does not depend on `module_ecdh`.

The `module_ecdh` provides `secp256k1_ecdh()` — a standard ECDH API that takes raw public keys. Blackcoin never calls this function (zero usage outside the secp256k1 library itself). Disabling it reduces the compiled library size and attack surface.

### Build output example (macOS x86_64)

```
Build Options:
  with external callbacks = no
  with benchmarks         = no
  with tests              = yes
  with ctime tests        = no
  with coverage           = no
  with examples           = no
  module ecdh             = no
  module recovery         = yes
  module extrakeys        = yes
  module schnorrsig       = yes
  module ellswift         = yes

  asm                     = x86_64
  ecmult window size      = 15
  ecmult gen table size   = 86 KiB
```

### Files

- `configure.ac:1714` — `--disable-module-ecdh` flag set explicitly
- `src/secp256k1/configure.ac:171-173` — `module_ecdh` defaults to `yes` but overridden by the configure.ac line above
- `src/secp256k1/include/secp256k1_ecdh.h` — standard ECDH API (unused by Blackcoin)
- `src/secp256k1/include/secp256k1_ellswift.h` — ElligatorSwift ECDH API (used by BIP324)
- `src/key.cpp` — `ComputeBIP324ECDHSecret()` calls `secp256k1_ellswift_xdh()`, not `secp256k1_ecdh()`
- `src/bip324.cpp` — BIP324 P2P v2 transport implementation