"""
PCAP extraction and Proxy MessageEnvelope serialization.
"""

from pathlib import Path
import socket
import struct
from typing import Any


class PcapPacket:
    """Structured representation of an extracted network packet from PCAP."""

    def __init__(
        self,
        seq_id: int,
        timestamp_ns: int,
        src_ip: str,
        src_port: int,
        dst_ip: str,
        dst_port: int,
        raw_payload: bytes,
        payload_text: str,
    ):
        self.seq_id = seq_id
        self.timestamp_ns = timestamp_ns
        self.src_ip = src_ip
        self.src_port = src_port
        self.dst_ip = dst_ip
        self.dst_port = dst_port
        self.raw_payload = raw_payload
        self.payload_text = payload_text

    def to_dict(self) -> dict[str, Any]:
        return {
            "id": self.seq_id,
            "timestampNs": self.timestamp_ns,
            "srcIp": self.src_ip,
            "srcPort": self.src_port,
            "dstIp": self.dst_ip,
            "dstPort": self.dst_port,
            "size": len(self.raw_payload),
            "payloadSummary": self.payload_text[:60] + "..." if len(self.payload_text) > 60 else self.payload_text,
            "payloadFull": self.payload_text,
        }

    def pack_envelope(self) -> bytes:
        """
        Packs payload and UDP metadata into standard Proxy MessageEnvelope wire format.
        EnvelopeHeader (18 bytes) + UdpMetadata (48 bytes) + raw payload.
        """
        src_ip_bytes = socket.inet_aton(self.src_ip)
        dst_ip_bytes = socket.inet_aton(self.dst_ip)
        src_port_net = struct.pack("!H", self.src_port)
        dst_port_net = struct.pack("!H", self.dst_port)

        meta = (
            src_ip_bytes
            + src_port_net
            + dst_port_net
            + dst_ip_bytes
            + (b"\x00" * 32)  # IPv6 bytes
            + b"\x04"  # addressFamily = 4 (IPv4)
            + (b"\x00" * 3)  # reserved[3]
        )
        assert len(meta) == 48

        header = struct.pack(
            "<HBBHIQ",
            0x5058,  # magic 'PX'
            1,  # version
            1,  # protocol = Udp
            len(meta),
            len(self.raw_payload),
            self.timestamp_ns,
        )
        assert len(header) == 18

        return header + meta + self.raw_payload


class PcapReader:
    """Zero-dependency parser for standard PCAP captures."""

    @staticmethod
    def read_packets(filepath: str | Path) -> list[PcapPacket]:
        packets: list[PcapPacket] = []
        p = Path(filepath)
        if not p.is_file():
            return packets

        with open(p, "rb") as f:
            global_hdr = f.read(24)
            if len(global_hdr) < 24:
                return packets

            magic = struct.unpack("<I", global_hdr[:4])[0]
            if magic in (0xA1B2C3D4, 0xA1B23C4D):
                endian = "<"
                is_nano = (magic == 0xA1B23C4D)
            elif magic in (0xD4C3B2A1, 0x4D3CB2A1):
                endian = ">"
                is_nano = (magic == 0x4D3CB2A1)
            else:
                return packets

            seq = 1
            while True:
                hdr_bytes = f.read(16)
                if len(hdr_bytes) < 16:
                    break
                ts_sec, ts_usec, incl_len, _ = struct.unpack(f"{endian}IIII", hdr_bytes)
                raw_pkt = f.read(incl_len)
                if len(raw_pkt) < 14:
                    continue

                # Parse Ethernet header
                eth_type = struct.unpack("!H", raw_pkt[12:14])[0]
                offset = 14
                if eth_type == 0x8100:  # VLAN tag
                    eth_type = struct.unpack("!H", raw_pkt[16:18])[0]
                    offset = 18

                if eth_type != 0x0800 or len(raw_pkt) < offset + 20:  # IPv4
                    continue

                ip_hdr = raw_pkt[offset : offset + 20]
                ihl = (ip_hdr[0] & 0x0F) * 4
                proto = ip_hdr[9]
                src_ip = socket.inet_ntoa(ip_hdr[12:16])
                dst_ip = socket.inet_ntoa(ip_hdr[16:20])

                if proto != 17 or len(raw_pkt) < offset + ihl + 8:  # UDP
                    continue

                udp_hdr = raw_pkt[offset + ihl : offset + ihl + 8]
                src_port, dst_port, udp_len, _ = struct.unpack("!HHHH", udp_hdr)
                payload_offset = offset + ihl + 8
                payload_len = max(0, udp_len - 8)
                payload = raw_pkt[payload_offset : payload_offset + payload_len]

                ts_ns = (ts_sec * 1_000_000_000) + (ts_usec if is_nano else ts_usec * 1_000)
                try:
                    text_preview = payload.decode("utf-8")
                except UnicodeDecodeError:
                    text_preview = payload.hex()

                packets.append(
                    PcapPacket(
                        seq_id=seq,
                        timestamp_ns=ts_ns,
                        src_ip=src_ip,
                        src_port=src_port,
                        dst_ip=dst_ip,
                        dst_port=dst_port,
                        raw_payload=payload,
                        payload_text=text_preview,
                    )
                )
                seq += 1

        return packets

