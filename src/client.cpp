#include "protocol.h"
#include "jitter_buffer.h"
#include "net_utils.h"
#include "metrics.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

struct ClientConfig {
    std::string host      = "127.0.0.1";
    int         port      = 9001;
    std::string proto     = "tcp";
    int         jitter_ms = 80;
    bool        debug     = false;
};

static ClientConfig parse_args(int argc, char** argv) {
    ClientConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a.rfind("--host=",   0) == 0) cfg.host      = a.substr(7);
        else if (a.rfind("--port=",   0) == 0) cfg.port      = std::stoi(a.substr(7));
        else if (a.rfind("--proto=",  0) == 0) cfg.proto     = a.substr(8);
        else if (a.rfind("--jitter=", 0) == 0) cfg.jitter_ms = std::stoi(a.substr(9));
        else if (a == "--debug")                cfg.debug     = true;
    }
    return cfg;
}

static int connect_to_server(const ClientConfig& cfg) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(cfg.port);
    inet_pton(AF_INET, cfg.host.c_str(), &addr.sin_addr);
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); exit(1); }
    set_tcp_nodelay(fd);
    LOG("Connected to %s:%d", cfg.host.c_str(), cfg.port);
    return fd;
}

static bool recv_handshake(int fd) {
    PacketHeader h{};
    if (!recv_all(fd, &h, sizeof(h))) return false;
    h.ntoh();
    if (!h.valid_magic() || static_cast<PacketType>(h.type) != PacketType::HANDSHAKE) {
        ERR("Bad handshake");
        return false;
    }
    LOG("Handshake OK");
    return true;
}

static void receiver_thread(int fd,
                              JitterBuffer& jbuf,
                              Metrics& metrics,
                              std::atomic<bool>& running) {
    uint32_t last_seq = 0;
    bool first = true;

    while (running) {
        PacketHeader h{};
        if (!recv_all(fd, &h, sizeof(h))) { LOG("Connection closed"); break; }
        h.ntoh();
        if (!h.valid_magic()) { ERR("Bad magic"); break; }

        PacketType type = static_cast<PacketType>(h.type);

        if (type == PacketType::GOODBYE) { LOG("Server sent GOODBYE"); break; }

        if (type == PacketType::HEARTBEAT) {
            if (h.payload_bytes > 0) {
                std::vector<uint8_t> tmp(h.payload_bytes);
                recv_all(fd, tmp.data(), h.payload_bytes);
            }
            continue;
        }

        if (type != PacketType::AUDIO) {
            if (h.payload_bytes > 0) {
                std::vector<uint8_t> tmp(h.payload_bytes);
                recv_all(fd, tmp.data(), h.payload_bytes);
            }
            continue;
        }

        if (h.payload_bytes != CHUNK_BYTES) { ERR("Bad payload size"); break; }

        std::vector<int16_t> pcm(CHUNK_SAMPLES);
        if (!recv_all(fd, pcm.data(), CHUNK_BYTES)) break;

        metrics.record_recv();

        if (!first && h.sequence_id != last_seq + 1)
            metrics.record_lost();
        last_seq = h.sequence_id;
        first = false;

        int32_t lat = (int32_t)(now_ms() - h.timestamp_ms);
        if (lat >= 0 && lat < 5000) metrics.record_latency_ms(lat);

        JitterPacket jpkt;
        jpkt.seq          = h.sequence_id;
        jpkt.timestamp_ms = h.timestamp_ms;
        jpkt.pcm          = std::move(pcm);
        jbuf.push(std::move(jpkt));
    }

    running = false;
    jbuf.close();
    LOG("Receiver thread exiting");
}

struct PlaybackCtx {
    JitterBuffer*        jbuf;
    Metrics*             metrics;
    std::vector<int16_t> last_frame;
    std::atomic<bool>*   running;
};

static void audio_callback(ma_device* device,
                            void* pOutput,
                            const void* /*pInput*/,
                            ma_uint32 /*frameCount*/) {
    auto* ctx = static_cast<PlaybackCtx*>(device->pUserData);
    int16_t* out = static_cast<int16_t*>(pOutput);
    auto pkt = ctx->jbuf->pop();
    if (pkt) {
        memcpy(out, pkt->pcm.data(), pkt->pcm.size() * sizeof(int16_t));
        ctx->last_frame = pkt->pcm;
    } else {
        if (!ctx->last_frame.empty())
            memcpy(out, ctx->last_frame.data(), ctx->last_frame.size() * sizeof(int16_t));
        else
            memset(out, 0, CHUNK_SAMPLES * sizeof(int16_t));
        ctx->metrics->record_lost();
    }
}

int main(int argc, char** argv) {
    ClientConfig cfg = parse_args(argc, argv);
    g_debug = cfg.debug;

    int fd = connect_to_server(cfg);
    if (!recv_handshake(fd)) { close(fd); return 1; }

    std::atomic<bool> running{true};
    JitterBuffer jbuf(cfg.jitter_ms);
    Metrics metrics;

    std::thread rcv([&] { receiver_thread(fd, jbuf, metrics, running); });

    PlaybackCtx ctx{&jbuf, &metrics, {}, &running};

    ma_device_config dcfg   = ma_device_config_init(ma_device_type_playback);
    dcfg.playback.format    = ma_format_s16;
    dcfg.playback.channels  = CHANNELS;
    dcfg.sampleRate         = SAMPLE_RATE;
    dcfg.periodSizeInFrames = CHUNK_FRAMES;
    dcfg.dataCallback       = audio_callback;
    dcfg.pUserData          = &ctx;

    ma_device device;
    if (ma_device_init(nullptr, &dcfg, &device) != MA_SUCCESS) {
        ERR("Failed to init audio device");
        running = false; jbuf.close(); rcv.join(); close(fd); return 1;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        ERR("Failed to start audio device");
        ma_device_uninit(&device);
        running = false; jbuf.close(); rcv.join(); close(fd); return 1;
    }

    LOG("Playback started (jitter=%dms)", cfg.jitter_ms);

    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        metrics.maybe_print();
    }

    ma_device_stop(&device);
    ma_device_uninit(&device);
    rcv.join();
    close(fd);
    LOG("Client shut down cleanly");
    return 0;
}
