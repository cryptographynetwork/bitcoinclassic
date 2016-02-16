// Copyright (c) 2015 G. Andrew Stone
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#pragma once
#ifndef BITCOIN_UNLIMITED_H
#define BITCOIN_UNLIMITED_H

#include "net.h"
#include <univalue.h>
#include <vector>

class CBlock;
class CBlockIndex;
class CValidationState;
class CDiskBlockPos;
class CNode;

// BUIP010 Xtreme Thinblocks:
extern bool HaveConnectThinblockNodes();
extern bool HaveThinblockNodes();
extern bool CheckThinblockTimer(uint256 hash);
extern bool ClearThinblockTimer(uint256 hash);
extern bool IsThinBlocksEnabled();
extern bool IsChainNearlySyncd();
extern void SendSeededBloomFilter(CNode *pto);
extern void HandleBlockMessage(CNode *pfrom, const std::string &strCommand, CBlock &block, const CInv &inv);
extern void ConnectToThinBlockNodes();
extern void CheckNodeSupportForThinBlocks();
extern void SendXThinBlock(CBlock &block, CNode* pfrom, const CInv &inv);

// Handle receiving and sending messages from thin block capable nodes only (so that thin block nodes capable nodes are preferred)
extern bool ThinBlockMessageHandler(std::vector<CNode*>& vNodesCopy);
extern std::map<uint256, uint64_t> mapThinBlockTimer;

#endif
