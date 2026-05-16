#pragma once
#include <cstdint>
#include <cstring>
#include <arpa/inet.h>

static constexpr uint8_t  MAGIC_0       = 0xAD;
static constexpr uint8_t  MAGIC_1       = 0xD0;
static constexpr uint8_t  PROTO_VERSION = 0x01;

static constexpr int SAMPLE_RATE   = 48000;
static constexpr int CHANNELS      = 2;
static constexpr int CHUNK_FRAMES  = 960;
static constexpr int CHUNK_SAMPLES = CHUNK_FRAMES * CHANNELS;
static constexpr int CHUNK_BYTES   = CHUNK_SAMPLES * (int)sizeof(int16_t);

enum class PacketType : uint8_t {
    HANDSHAKE = 0x01,
    AUDIO     = 0x02,
    HEARTBEAT = 0x03,
    GOODBYE   = 0x04,
};

#pragma pack(push, 1)
struct PacketHeader {
    uint8_t  magic[2];
    uint8_t  version;
    uint8_t  type;
    uint32_t sequence_id;
    uint32_t timestamp_ms;
    uint32_t payload_bytes;

    void hton() {
        sequence_id   = htonl(sequence_id);
        timestamp_ms  = htonl(timestamp_ms);
        payload_bytes = htonl(payload_bytes);
    }
    void ntoh() {
        sequence_id   = ntohl(sequence_id);
        timestamp_ms  = ntohl(timestamp_ms);
        payload_bytes = ntohl(payload_bytes);
    }
    bool valid_magic() const {
        return magic[0] == MAGIC_0 && magic[1] == MAGIC_1;
    }
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 16, "PacketHeader must be 16 bytes");

inline PacketHeader make_header(PacketType t, uint32_t seq, uint32_t ts_ms, uint32_t payload_len) {
    PacketHeader h{};
    h.magic[0]      = MAGIC_0;
    h.magic[1]      = MAGIC_1;
    h.version       = PROTO_VERSION;
    h.type          = static_cast<uint8_t>(t);
    h.sequence_id   = seq;
    h.timestamp_ms  = ts_ms;
    h.payload_bytes = payload_len;
    return h;
}
