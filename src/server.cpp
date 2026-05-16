#include "protocol.h"
#include "blocking_queue.h"
#include "net_utils.h"
#include "metrics.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include <sndfile.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

struct AudioPacket {
    uint32_t             seq;
    uint32_t             timestamp_ms;
    std::vector<int16_t> pcm;
};

struct ServerConfig {
    std::string source    = "";
    int         port      = 9001;
    std::string proto     = "tcp";
    std::string bind_addr = "0.0.0.0";
    bool        loop      = true;
    bool        debug     = false;
};

static ServerConfig parse_args(int argc, char** argv) {
    ServerConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a.rfind("--source=", 0) == 0) cfg.source    = a.substr(9);
        else if (a.rfind("--port=",   0) == 0) cfg.port      = std::stoi(a.substr(7));
        else if (a.rfind("--proto=",  0) == 0) cfg.proto     = a.substr(8);
        else if (a.rfind("--bind=",   0) == 0) cfg.bind_addr = a.substr(7);
        else if (a == "--no-loop")              cfg.loop      = false;
        else if (a == "--debug")                cfg.debug     = true;
    }
    return cfg;
}

static void capture_thread(const ServerConfig& cfg,
                            BlockingQueue<AudioPacket>& queue,
                            std::atomic<bool>& running) {
    if (cfg.source.rfind("file:", 0) != 0) {
        ERR("Only --source=file:<path> is supported");
        running = false;
        return;
    }

    std::string path = cfg.source.substr(5);
    uint32_t seq = 0;

    while (running) {
        SF_INFO info{};
        SNDFILE* sf = sf_open(path.c_str(), SFM_READ, &info);
        if (!sf) {
            ERR("Cannot open WAV file: %s", path.c_str());
            running = false;
            return;
        }
        if (info.samplerate != SAMPLE_RATE) {
            ERR("WAV sample rate %d != expected %d", info.samplerate, SAMPLE_RATE);
            sf_close(sf);
            running = false;
            return;
        }
        LOG("Streaming: %s (channels=%d, frames=%lld)",
            path.c_str(), info.channels, (long long)info.frames);

        std::vector<int16_t> buf(CHUNK_SAMPLES);

        while (running) {
            sf_count_t got = sf_readf_short(sf, buf.data(), CHUNK_FRAMES);
            if (got <= 0) break;

            if (info.channels == 1) {
                for (int i = (int)got - 1; i >= 0; --i)
                    buf[i*2+1] = buf[i*2] = buf[i];
            }
            if (got < CHUNK_FRAMES) {
                memset(buf.data() + got * CHANNELS, 0,
                       (CHUNK_FRAMES - got) * CHANNELS * sizeof(int16_t));
            }

            AudioPacket pkt;
            pkt.seq          = seq++;
            pkt.timestamp_ms = now_ms();
            pkt.pcm          = buf;

            DLOG("Captured seq=%u ts=%u", pkt.seq, pkt.timestamp_ms);
            if (!queue.push(std::move(pkt))) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        sf_close(sf);
        if (!cfg.loop || !running) break;
        LOG("Looping WAV file");
    }

    queue.close();
    LOG("Capture thread exiting");
}

static void sender_thread(BlockingQueue<AudioPacket>& queue,
                           std::vector<int>& clients,
                           std::mutex& clients_mu,
                           Metrics& metrics,
                           std::atomic<bool>& running) {
    while (running) {
        auto item = queue.pop();
        if (!item) break;
        const AudioPacket& pkt = *item;

        PacketHeader hdr = make_header(
            PacketType::AUDIO, pkt.seq, pkt.timestamp_ms,
            static_cast<uint32_t>(pkt.pcm.size() * sizeof(int16_t)));
        hdr.hton();

        std::unique_lock<std::mutex> lock(clients_mu);
        std::vector<int> dead;
        for (int fd : clients) {
            bool ok = send_all(fd, &hdr, sizeof(hdr));
            if (ok) ok = send_all(fd, pkt.pcm.data(), pkt.pcm.size() * sizeof(int16_t));
            if (!ok) {
                LOG("Client fd=%d disconnected", fd);
                close(fd);
                dead.push_back(fd);
            } else {
                metrics.record_sent();
            }
        }
        for (int fd : dead)
            clients.erase(std::remove(clients.begin(), clients.end(), fd), clients.end());
        lock.unlock();
        metrics.maybe_print();
    }
    LOG("Sender thread exiting");
}

static void send_handshake(int fd) {
    PacketHeader hdr = make_header(PacketType::HANDSHAKE, 0, now_ms(), 0);
    hdr.hton();
    send_all(fd, &hdr, sizeof(hdr));
}

static int make_listen_socket(const ServerConfig& cfg) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(cfg.port);
    inet_pton(AF_INET, cfg.bind_addr.c_str(), &addr.sin_addr);
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    listen(sock, 8);
    LOG("Listening on %s:%d (TCP)", cfg.bind_addr.c_str(), cfg.port);
    return sock;
}

int main(int argc, char** argv) {
    ServerConfig cfg = parse_args(argc, argv);
    g_debug = cfg.debug;

    if (cfg.source.empty()) {
        fprintf(stderr, "Usage: audiostream_server --source=file:<path> [--port=9001] [--debug]\n");
        return 1;
    }

    std::atomic<bool> running{true};
    BlockingQueue<AudioPacket> queue(64);
    std::vector<int> clients;
    std::mutex clients_mu;
    Metrics metrics;

    std::thread cap([&] { capture_thread(cfg, queue, running); });
    std::thread snd([&] { sender_thread(queue, clients, clients_mu, metrics, running); });

    int listen_fd = make_listen_socket(cfg);

    while (running) {
        sockaddr_in peer{};
        socklen_t plen = sizeof(peer);
        int cfd = accept(listen_fd, (sockaddr*)&peer, &plen);
        if (cfd < 0) { if (running) perror("accept"); break; }
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        LOG("Client connected: %s:%d (fd=%d)", ip, ntohs(peer.sin_port), cfd);
        set_tcp_nodelay(cfd);
        send_handshake(cfd);
        std::unique_lock<std::mutex> lock(clients_mu);
        clients.push_back(cfd);
    }

    running = false;
    queue.close();
    close(listen_fd);
    cap.join();
    snd.join();
    LOG("Server shut down cleanly");
    return 0;
}
