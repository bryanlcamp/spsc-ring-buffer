#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
#include <variant>

namespace hft::protocols::cme {

#pragma pack(push, 1)

// --- REPLICATED RAW CME MDP 3.0 WIRE FORMAT STRUCTS ---

struct CmePacketHeader {
    uint32_t msgSeqNum;      // Unique packet wire sequence index
    uint64_t sendingTime;    // Nanosecond wire ingestion snapshot assigned by exchange
};

struct SbeMessageHeader {
    uint16_t blockLength;    // Fixed size of the core message root
    uint16_t templateId;     // CME template schema index (e.g., Template 42)
    uint16_t schemaId;       // Typically 197 for MDP 3.0
    uint16_t version;        // Schema versioning tracker
};

struct SbeGroupHeader {
    uint16_t blockLength;    // Fixed length size of an individual repeating entry
    uint16_t numInGroup;     // Count of entries packed sequentially inside the block
};

struct CmeMdPriceLevel {
    int64_t  mdEntryPx;       // Raw price scaled by 10^7 (scaled integer standard)
    int32_t  mdEntrySize;     // Contract Quantity / Volume resting at this level
    int32_t  numberOfOrders;  // Number of real orders forming this layer block
    uint16_t mdPriceLevel;    // Depth book index level layer (1 to 10)
    char     mdUpdateAction;  // New=0, Change=1, Delete=2
    char     mdEntryType;     // Bid='0', Ask='1', Trade='2'
};

struct CmeMdIncrementalRefresh {
    uint64_t transactTime;        // Ingress execution time from CME matching engine core
    uint32_t matchEventIndicator;  // Bitfield flag indicating final packet block states
};

template <bool TrackTelemetry = false>
struct CmeMarketPacket {
    CmePacketHeader         packetHeader;
    SbeMessageHeader        sbeHeader;
    CmeMdIncrementalRefresh payload;
    SbeGroupHeader          groupHeader;
    CmeMdPriceLevel         bookLevels[3]; // Populated 3-level deep order book update block
    
    // Non-invasive compile-time telemetry tracking hook
    [[no_unique_address]] std::conditional_t<TrackTelemetry, uint64_t, std::monostate> pipelineEntryNs;
};

#pragma pack(pop)

// --- HIGH-PERFORMANCE FLAT APPLICATION DOMAIN STRUCTURES ---

struct NormalizedBookLevel {
    int64_t  price;
    int32_t  quantity;
    uint16_t level;
    char     side;   // 'B' = Bid, 'A' = Ask, 'T' = Trade
    char     action; // 'N' = New, 'C' = Change, 'D' = Delete
};

/**
 * @brief The clean, simplified event structure emitted straight to the application strategy layer.
 * Stripped of exchange-specific wire padding to minimize L1 data cache footprint.
 */
struct BookUpdateEvent {
    uint64_t            exchangeTime;
    uint32_t            wireSequence;
    uint16_t            activeLevelsCount;
    NormalizedBookLevel levels[3]; 
};

class CmeBinaryDecoder {
public:
    /**
     * @brief Zero-allocation, branchless translation loop parsing.
     * Takes raw wire frames and writes normalized events directly into an application-allocated buffer slot.
     */
    template <bool TrackTelemetry>
    inline void decode(const CmeMarketPacket<TrackTelemetry>& packet, BookUpdateEvent& outEvent) noexcept {
        // Unpack wire header constants into your application's domain layer
        outEvent.wireSequence = packet.packetHeader.msgSeqNum;
        outEvent.exchangeTime = packet.payload.transactTime;
        
        const uint16_t levelsToParse = packet.groupHeader.numInGroup;
        outEvent.activeLevelsCount = levelsToParse;

        // Flatten the repeating group structures straight to application frames branchlessly
        for (uint16_t i = 0; i < levelsToParse; ++i) {
            const auto& wireLevel = packet.bookLevels[i];
            auto& appLevel = outEvent.levels[i];

            appLevel.price = wireLevel.mdEntryPx;
            appLevel.quantity = wireLevel.mdEntrySize;
            appLevel.level = wireLevel.mdPriceLevel;

            // Direct branchless mapping of exchange char tags to clear domain primitives
            appLevel.side = (wireLevel.mdEntryType == '0') ? 'B' : ((wireLevel.mdEntryType == '1') ? 'A' : 'T');
            appLevel.action = (wireLevel.mdUpdateAction == 0) ? 'N' : ((wireLevel.mdUpdateAction == 1) ? 'C' : 'D');
        }
    }
};

} // namespace hft::protocols::cme