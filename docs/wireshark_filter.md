# Wireshark filters for AudioStream

## Capture only AudioStream traffic on loopback
```
tcp.port == 9001
```

## Filter by magic bytes (0xAD 0xD0 at offset 0 of TCP payload)
```
tcp.port == 9001 and tcp.payload[0:2] == ad:d0
```

## Show only AUDIO packets (type byte = 0x02 at offset 3)
```
tcp.port == 9001 and tcp.payload[3] == 02
```

## Show only HANDSHAKE packets
```
tcp.port == 9001 and tcp.payload[3] == 01
```

## Decode sequence IDs (bytes 4–7, big-endian uint32)
Add a custom column in Wireshark:
- Field name: `tcp.payload[4:4]`
- Title: `seq_id`

## One-liner tcpdump packet rate monitor
```bash
tcpdump -i lo port 9001 -q | pv -l -r > /dev/null
```
Expected: ~50 packets/sec (one 20 ms chunk per packet).

## Measure round-trip with ping at audio chunk rate
```bash
ping -i 0.02 192.168.1.42   # one ping per 20 ms = one audio chunk interval
```