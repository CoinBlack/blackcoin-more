// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_DRIVECHAIN_H
#define BITCOIN_SCRIPT_DRIVECHAIN_H

#include "serialize.h"
#include "primitives/transaction.h"
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

const unsigned char ACK_LABEL[] = {0x41, 0x43, 0x4B, 0x3A}; // "ACK:"
const size_t ACK_LABEL_LENGTH = sizeof(ACK_LABEL);

class Ack
{
public:
    std::vector<unsigned char> prefix;
    std::vector<unsigned char> preimage;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        uint64_t nPayload = 0;
        if (!ser_action.ForRead())
            nPayload = CalcPayloadSize();
        READWRITE(COMPACTSIZE(nPayload));
        READWRITE(prefix);
        // Empty preimage should not be serialized
        if (ser_action.ForRead()) {
            uint64_t nPrefix = prefix.size();
            nPrefix += GetSizeOfCompactSize(nPrefix);
            if (nPayload > nPrefix)
                READWRITE(preimage);
            if (CalcPayloadSize() != nPayload)
                throw std::runtime_error("Not valid ACK");
        } else {
            if (preimage.size() > 0)
                READWRITE(preimage);
        }
    }

    uint64_t CalcPayloadSize() const
    {
        uint64_t nPayload = 0;
        nPayload += GetSizeOfCompactSize(prefix.size());
        nPayload += prefix.size();
        // Empty preimage should not be serialized
        if (!preimage.empty()) {
            nPayload += GetSizeOfCompactSize(preimage.size());
            nPayload += preimage.size();
        }
        return nPayload;
    }

    uint64_t CalcSize() const
    {
        uint64_t nPayload = CalcPayloadSize();
        return GetSizeOfCompactSize(nPayload) + nPayload;
    }

    Ack() {}
    Ack(std::vector<unsigned char> prefix, std::vector<unsigned char> preimage = std::vector<unsigned char>())
        : prefix(prefix), preimage(preimage)
    {
    }
};

class AckList
{
public:
    std::vector<Ack> vAck;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        uint64_t sizePayload = 0;
        if (!ser_action.ForRead())
            sizePayload = CalcPayloadSize();
        READWRITE(COMPACTSIZE(sizePayload));
        if (ser_action.ForRead()) {
            uint64_t read = 0;
            while (read < sizePayload) {
                Ack ack;
                READWRITE(ack);
                read += ack.CalcSize();
                vAck.push_back(ack);
            }
            if (read != sizePayload)
                throw std::runtime_error("Not valid ACK LIST");
        } else {
            for (Ack& ack : vAck) {
                READWRITE(ack);
            }
        }
    }

    uint64_t CalcPayloadSize() const
    {
        uint64_t nPayload = 0;
        for (const Ack& ack : vAck) {
            nPayload += ack.CalcSize();
        }
        return nPayload;
    }

    uint64_t CalcSize() const
    {
        uint64_t nSize = 0;
        uint64_t nPayloadSize = CalcPayloadSize();
        nSize += GetSizeOfCompactSize(nPayloadSize);
        nSize += nPayloadSize;
        return nSize;
    }

    AckList() {}
    AckList(std::vector<Ack> acks) : vAck(acks) {}
};

class ChainAckList
{
public:
    std::vector<unsigned char> chainId;
    AckList ackList;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        uint64_t nPayload = 0;
        if (!ser_action.ForRead())
            nPayload = CalcPayloadSize();
        READWRITE(COMPACTSIZE(nPayload));
        READWRITE(chainId);
        READWRITE(ackList);
        if (ser_action.ForRead() && nPayload != CalcPayloadSize())
            throw std::runtime_error("Not valid CHAIN ACK LIST");
    }

    uint64_t CalcPayloadSize() const
    {
        uint64_t nPayload = 0;
        nPayload += GetSizeOfCompactSize(chainId.size());
        nPayload += chainId.size();
        nPayload += ackList.CalcSize();
        return nPayload;
    }

    uint64_t CalcSize() const
    {
        uint64_t nSize = 0;
        uint64_t nPayloadSize = CalcPayloadSize();
        nSize += GetSizeOfCompactSize(nPayloadSize);
        nSize += nPayloadSize;
        return nSize;
    }

    ChainAckList& operator<<(Ack ack)
    {
        ackList.vAck.push_back(ack);
        return *this;
    }

    ChainAckList() {}
    ChainAckList(std::vector<unsigned char> chainId) : chainId(chainId) {}
};

class FullAckList
{
public:
    std::vector<ChainAckList> vChainAcks;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        uint64_t sizePayload = 0;
        if (!ser_action.ForRead())
            sizePayload = CalcPayloadSize();
        READWRITE(COMPACTSIZE(sizePayload));
        if (ser_action.ForRead()) {
            uint64_t read = 0;
            while (read < sizePayload) {
                ChainAckList chainAcks;
                READWRITE(chainAcks);
                read += chainAcks.CalcSize();
                vChainAcks.push_back(chainAcks);
            }
            if (read != sizePayload)
                throw std::runtime_error("Not valid FULL ACK LIST");
        } else {
            for (auto& chainAcks : vChainAcks) {
                READWRITE(chainAcks);
            }
        }
    }

    uint64_t CalcPayloadSize() const
    {
        uint64_t nPayloadSize = 0;
        for (const auto& chainAcks : vChainAcks) {
            nPayloadSize += chainAcks.CalcSize();
        }
        return nPayloadSize;
    }

    uint64_t CalcSize() const
    {
        uint64_t nSize = 0;
        uint64_t nPayloadSize = CalcPayloadSize();
        nSize += GetSizeOfCompactSize(nPayloadSize);
        nSize += nPayloadSize;
        return nSize;
    }

    FullAckList& operator<<(Ack ack)
    {
        if (!vChainAcks.empty()) {
            vChainAcks.rbegin()[0].ackList.vAck.push_back(ack);
        } else {
            throw std::runtime_error("Empty Chain");
        }
        return *this;
    }

    FullAckList& operator<<(ChainAckList chainAckList)
    {
        vChainAcks.push_back(chainAckList);
        return *this;
    }

    FullAckList() {}
};

class BaseBlockReader
{
public:
    virtual int GetBlockNumber() const
    {
        return -1;
    }

    virtual CTransaction GetBlockCoinbase(int blockNumber) const
    {
        return CTransaction();
    }
};

bool CountAcks(const std::vector<unsigned char> hashSpend, const std::vector<unsigned char>& chainId, int periodAck, int periodLiveness, int& positiveAcks, int& negativeAcks, const BaseBlockReader& blockReader);

#endif // BITCOIN_SCRIPT_DRIVECHAIN_H
