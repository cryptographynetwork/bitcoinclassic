// Copyright (c) 2015 G. Andrew Stone
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "clientversion.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "main.h"
#include "net.h"
#include "primitives/block.h"
#include "rpcserver.h"
#include "thinblock.h"
#include "tinyformat.h"
#include "txmempool.h"
#include "unlimited.h"
#include "utilstrencodings.h"
#include "util.h"
#include "validationinterface.h"
#include "version.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <inttypes.h>

using namespace std;

// BUIP010 Xtreme Thinblocks Variables
std::map<uint256, uint64_t> mapThinBlockTimer;

/**
 *  BUIP010 Xtreme Thinblocks Section 
 */
bool HaveConnectThinblockNodes()
{
    // Strip the port from then list of all the current in and outbound ip addresses
    std::vector<std::string> vNodesIP;
    {
        LOCK(cs_vNodes);
        BOOST_FOREACH (CNode* pnode, vNodes) {
           int pos = pnode->addrName.find(":");
           if (pos <= 0 )
               vNodesIP.push_back(pnode->addrName);
           else
               vNodesIP.push_back(pnode->addrName.substr(0, pos));
        }
    }

    // Create a set used to check for cross connected nodes.
    // A cross connected node is one where we have a connect-thinblock connection to
    // but we also have another inbound connection which is also using
    // connect-thinblock. In those cases we have created a dead-lock where no blocks 
    // can be downloaded unless we also have at least one additional connect-thinblock 
    // connection to a different node.
    std::set<std::string> nNotCrossConnected;
 
    int nConnectionsOpen = 0;
    BOOST_FOREACH(const std::string& strAddrNode, mapMultiArgs["-connect-thinblock"]) {
        std::string strThinblockNode;
        int pos = strAddrNode.find(":");
        if (pos <= 0 )
            strThinblockNode = strAddrNode;
        else
            strThinblockNode = strAddrNode.substr(0, pos);
        BOOST_FOREACH(std::string strAddr, vNodesIP) {
            if (strAddr == strThinblockNode) {
                nConnectionsOpen++;
                if (!nNotCrossConnected.count(strAddr))
                    nNotCrossConnected.insert(strAddr);
                else
                    nNotCrossConnected.erase(strAddr);
            }
        }
    }
    if (nNotCrossConnected.size() > 0)
        return true;
    else if (nConnectionsOpen > 0)
        LogPrint("thin", "You have a cross connected thinblock node - we may download regular blocks until you resolve the issue\n");
    return false; // Connections are either not open or they are cross connected.
} 

bool HaveThinblockNodes()
{
    {
        LOCK(cs_vNodes);
        BOOST_FOREACH (CNode* pnode, vNodes)
            if (pnode->nVersion >= THINBLOCKS_VERSION)
                return true;
    }
    return false;
}

bool CheckThinblockTimer(uint256 hash)
{
    if (!mapThinBlockTimer.count(hash)) {
        mapThinBlockTimer[hash] = GetTimeMillis();
        LogPrint("thin", "Starting Preferential Thinblock timer\n");
    }
    else {
        // Check that we have not exceeded the 10 second limit.
        // If we have then we want to return false so that we can
        // proceed to download a regular block instead.
        uint64_t elapsed = GetTimeMillis() - mapThinBlockTimer[hash];
        if (elapsed > 10000) {
            LogPrint("thin", "Preferential Thinblock timer exceeded - downloading regular block instead\n");
            return false;
        }
    }
    return true;
}

bool ClearThinBlockTimer(uint256 hash)
{
    if (mapThinBlockTimer.count(hash)) {
        mapThinBlockTimer.erase(hash);
        LogPrint("thin", "Clearing Preferential Thinblock timer\n");
    }
}

bool IsThinBlocksEnabled() 
{
    return GetBoolArg("-use-thinblocks", true);
}

bool IsChainNearlySyncd() 
{
    LOCK(cs_main);
    if(chainActive.Height() < pindexBestHeader->nHeight - 2)
        return false;
    return true;
}

void SendSeededBloomFilter(CNode *pto)
{
    LogPrint("thin", "Starting creation of bloom filter\n");
    seed_insecure_rand();
    CBloomFilter memPoolFilter;
    double nBloomPoolSize = (double)mempool.mapTx.size();
    if (nBloomPoolSize > MAX_BLOOM_FILTER_SIZE / 1.8)
        nBloomPoolSize = MAX_BLOOM_FILTER_SIZE / 1.8;
    double nBloomDecay = 1.5 - (nBloomPoolSize * 1.8 / MAX_BLOOM_FILTER_SIZE);  // We should never go below 0.5 as we will start seeing re-requests for tx's
    int nElements = std::max((int)((int)mempool.mapTx.size() * nBloomDecay), 1); // Must make sure nElements is greater than zero or will assert
                                                                // TODO: we should probably rather fix the bloom.cpp constructor
    double nFPRate = .001 + (((double)nElements * 1.8 / MAX_BLOOM_FILTER_SIZE) * .004); // The false positive rate in percent decays as the mempool grows
    memPoolFilter = CBloomFilter(nElements, nFPRate, insecure_rand(), BLOOM_UPDATE_ALL);
    LogPrint("thin", "Bloom multiplier: %f FPrate: %f Num elements in bloom filter: %d num mempool entries: %d\n", nBloomDecay, nFPRate, nElements, (int)mempool.mapTx.size());

    // Seed the filter with the transactions in the memory pool, orphan pool and relay pool
    LOCK2(cs_main, pto->cs_filter);
    std::vector<uint256> memPoolHashes;
    mempool.queryHashes(memPoolHashes);
    for (uint64_t i = 0; i < memPoolHashes.size(); i++)
         memPoolFilter.insert(memPoolHashes[i]);

    LogPrint("thin", "Sending bloom filter: %d bytes peer=%d\n",::GetSerializeSize(memPoolFilter, SER_NETWORK, PROTOCOL_VERSION), pto->id);
    pto->PushMessage(NetMsgType::FILTERLOAD, memPoolFilter);
}

void HandleBlockMessage(CNode *pfrom, const string &strCommand, CBlock &block, const CInv &inv)
{
    int64_t startTime = GetTimeMicros();
    CValidationState state;
    // Process all blocks from whitelisted peers, even if not requested,
    // unless we're still syncing with the network.
    // Such an unrequested block may still be processed, subject to the
    // conditions in AcceptBlock().
    bool forceProcessing = pfrom->fWhitelisted && !IsInitialBlockDownload();
    const CChainParams& chainparams = Params();
    ProcessNewBlock(state, chainparams, pfrom, &block, forceProcessing, NULL);
    int nDoS;
    if (state.IsInvalid(nDoS)) {
        LogPrintf("Invalid block due to %s\n", state.GetRejectReason().c_str());
        pfrom->PushMessage("reject", strCommand, state.GetRejectCode(),
                           state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
        if (nDoS > 0) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), nDoS);
        }
    }
    LogPrint("thin", "Processed thinblock %s in %.2f seconds\n", inv.hash.ToString(), (double)(GetTimeMicros() - startTime) / 1000000.0);
    
    // When we request a thinblock we may get back a regular block if it is smaller than a thinblock
    // Therefore we have to remove the thinblock in flight if it exists and we also need to check that 
    // the block didn't arrive from some other peer.  This code ALSO cleans up the thin block that
    // was passed to us (&block), so do not use it after this.
    {
        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes) {
            if (pnode->mapThinBlocksInFlight.count(inv.hash)) {
                pnode->mapThinBlocksInFlight.erase(inv.hash); 
                pnode->thinBlockWaitingForTxns = -1;
                pnode->thinBlock.SetNull();
                if (pnode != pfrom) LogPrintf("Removing thinblock in flight %s from %s (%d)\n",inv.hash.ToString(), pnode->addrName.c_str(), pnode->id);
            }
        }
    }

    // Clear the thinblock timer used for preferential download
    ClearThinBlockTimer(inv.hash);
}

bool ThinBlockMessageHandler(vector<CNode*>& vNodesCopy)
{
  bool sleep = true;
  CNodeSignals& signals = GetNodeSignals();
  BOOST_FOREACH (CNode* pnode, vNodesCopy)
    {
      if ((pnode->fDisconnect)||(!pnode->ThinBlockCapable()))
        continue;

      // Receive messages
      {
        TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
        if (lockRecv)
          {
            if (!signals.ProcessMessages(pnode))
              pnode->CloseSocketDisconnect();

            if (pnode->nSendSize < SendBufferSize())
              {
                if (!pnode->vRecvGetData.empty() || (!pnode->vRecvMsg.empty() && pnode->vRecvMsg[0].complete()))
                  {
                    sleep = false;
                  }
              }
          }
      }
      boost::this_thread::interruption_point();
      signals.SendMessages(pnode);
      boost::this_thread::interruption_point();
    }
  return sleep;
}

void ConnectToThinBlockNodes()
{
    // Connect to specific addresses
    if (mapArgs.count("-connect-thinblock") && mapMultiArgs["-connect-thinblock"].size() > 0)
    {
        BOOST_FOREACH(const std::string& strAddr, mapMultiArgs["-connect-thinblock"])
        {
            CAddress addr;
            OpenNetworkConnection(addr, NULL, strAddr.c_str());
            MilliSleep(500);
        }
    }
}

void CheckNodeSupportForThinBlocks()
{
    // Check that a nodes pointed to with connect-thinblock actually supports thinblocks
    BOOST_FOREACH(string& strAddr, mapMultiArgs["-connect-thinblock"]) {
        if(CNode* pnode = FindNode(strAddr)) {
            if(pnode->nVersion < THINBLOCKS_VERSION && pnode->nVersion > 0) {
                LogPrintf("ERROR: You are trying to use connect-thinblocks but to a node that does not support it - Protocol Version: %d peer=%d\n", 
                           pnode->nVersion, pnode->id);
            }
        }
    }
}

void SendXThinBlock(CBlock &block, CNode* pfrom, const CInv &inv)
{
    if (inv.type == MSG_XTHINBLOCK)
    {
        CXThinBlock xThinBlock(block, *pfrom->pfilter);
        int nSizeBlock = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
        if (xThinBlock.collision == true) // If there is a cheapHash collision in this block then send a normal thinblock
        {
            CThinBlock thinBlock(block, *pfrom->pfilter);
            int nSizeThinBlock = ::GetSerializeSize(xThinBlock, SER_NETWORK, PROTOCOL_VERSION);
            if (nSizeThinBlock < nSizeBlock) {
                pfrom->PushMessage(NetMsgType::THINBLOCK, thinBlock);
                LogPrint("thin", "TX HASH COLLISION: Sent thinblock - size: %d vs block size: %d => tx hashes: %d transactions: %d  peerid=%d\n", nSizeThinBlock, nSizeBlock, xThinBlock.vTxHashes.size(), xThinBlock.mapMissingTx.size(), pfrom->id);
            }
            else {
                pfrom->PushMessage(NetMsgType::BLOCK, block);
                LogPrint("thin", "Sent regular block instead - xthinblock size: %d vs block size: %d => tx hashes: %d transactions: %d  peerid=%d\n", nSizeThinBlock, nSizeBlock, xThinBlock.vTxHashes.size(), xThinBlock.mapMissingTx.size(), pfrom->id);
            }
        }
        else // Send an xThinblock
        {
            // Only send a thinblock if smaller than a regular block
            int nSizeThinBlock = ::GetSerializeSize(xThinBlock, SER_NETWORK, PROTOCOL_VERSION);
            if (nSizeThinBlock < nSizeBlock) {
                pfrom->PushMessage(NetMsgType::XTHINBLOCK, xThinBlock);
                LogPrint("thin", "Sent xthinblock - size: %d vs block size: %d => tx hashes: %d transactions: %d  peerid=%d\n", nSizeThinBlock, nSizeBlock, xThinBlock.vTxHashes.size(), xThinBlock.mapMissingTx.size(), pfrom->id);
            }
            else {
                pfrom->PushMessage(NetMsgType::BLOCK, block);
                LogPrint("thin", "Sent regular block instead - xthinblock size: %d vs block size: %d => tx hashes: %d transactions: %d  peerid=%d\n", nSizeThinBlock, nSizeBlock, xThinBlock.vTxHashes.size(), xThinBlock.mapMissingTx.size(), pfrom->id);
            }
        }
    }
    else if (inv.type == MSG_THINBLOCK)
    {
        CThinBlock thinBlock(block, *pfrom->pfilter);
        int nSizeBlock = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
        int nSizeThinBlock = ::GetSerializeSize(thinBlock, SER_NETWORK, PROTOCOL_VERSION);
        if (nSizeThinBlock < nSizeBlock) { // Only send a thinblock if smaller than a regular block
            pfrom->PushMessage(NetMsgType::THINBLOCK, thinBlock);
            LogPrint("thin", "Sent thinblock - size: %d vs block size: %d => tx hashes: %d transactions: %d  peerid=%d\n", nSizeThinBlock, nSizeBlock, thinBlock.vTxHashes.size(), thinBlock.mapMissingTx.size(), pfrom->id);
        }
        else {
            pfrom->PushMessage(NetMsgType::BLOCK, block);
            LogPrint("thin", "Sent regular block instead - thinblock size: %d vs block size: %d => tx hashes: %d transactions: %d  peerid=%d\n", nSizeThinBlock, nSizeBlock, thinBlock.vTxHashes.size(), thinBlock.mapMissingTx.size(), pfrom->id);
        }
    }

}


