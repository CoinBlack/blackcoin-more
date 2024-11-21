// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/messages.h>
#include <core_io.h>
#include <node/context.h>
#include <policy/feerate.h>
#include <policy/fees.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <txmempool.h>
#include <univalue.h>
#include <validationinterface.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

using common::FeeModeFromString;
using common::FeeModesDetail;
using common::InvalidEstimateModeErrorMessage;
using node::NodeContext;

void RegisterFeeRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
