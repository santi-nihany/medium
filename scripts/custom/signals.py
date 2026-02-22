#!/usr/bin/env python3
"""Read and visualize .sig files produced by program/src/utils/sig.c."""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt

SIG_MAGIC = b"SIG1"
SIG_HEADER_SIZE_BYTES = 32
SIG_EDGE_EXTENDED_16 = 0xFFFF

SIG_FLAG_START_LEVEL = 1 << 0
SIG_FLAG_HAS_METADATA = 1 << 1
SIG_FLAG_HAS_PROFILE = 1 << 2

SIG_SIGNAL_TYPE_IR = 1
SIG_SIGNAL_TYPE_RF = 2

SIG_META_NEC_ADDR = 1
SIG_META_NEC_CMD = 2
SIG_META_RF_FREQ_HZ = 16
SIG_META_RF_MODULATION = 17
SIG_META_RF_PRINCETON_KEY = 18
SIG_META_RF_PRINCETON_TE_US = 19
SIG_META_RF_PRINCETON_GUARD = 20
SIG_META_RF_PRINCETON_BITS = 21

SIG_RF_MOD_AM270 = 1
SIG_RF_MOD_AM650 = 2


@dataclass
class SigRecord:
    signal_type: int
    flags: int
    tick_scale: int
    edge_count: int
    edges: list[int]
    metadata: bytes
    payload_crc_stored: int
    payload_crc_calc: int
    header_crc_stored: int
    header_crc_calc: int


def crc32_update(crc: int, data: bytes) -> int:
    for value in data:
        crc ^= value
        for _ in range(8):
            lsb = crc & 1
            crc >>= 1
            if lsb:
                crc ^= 0xEDB88320
    return crc & 0xFFFFFFFF


def crc32_block(data: bytes) -> int:
    return crc32_update(0xFFFFFFFF, data) ^ 0xFFFFFFFF


def signal_type_name(value: int) -> str:
    if value == SIG_SIGNAL_TYPE_IR:
        return "IR"
    if value == SIG_SIGNAL_TYPE_RF:
        return "RF"
    return f"UNKNOWN({value})"


def parse_tlv_metadata(raw: bytes) -> tuple[list[dict[str, object]], str | None]:
    items: list[dict[str, object]] = []
    i = 0
    while i < len(raw):
        if i + 2 > len(raw):
            return items, f"Malformed TLV at offset {i}: missing type/len."
        tlv_type = raw[i]
        tlv_len = raw[i + 1]
        start = i + 2
        end = start + tlv_len
        if end > len(raw):
            return items, f"Malformed TLV at offset {i}: len overruns metadata."
        value = raw[start:end]
        decoded_name = {
            SIG_META_NEC_ADDR: "NEC_ADDR",
            SIG_META_NEC_CMD: "NEC_CMD",
            SIG_META_RF_FREQ_HZ: "RF_FREQ_HZ",
            SIG_META_RF_MODULATION: "RF_MODULATION",
            SIG_META_RF_PRINCETON_KEY: "RF_PRINCETON_KEY",
            SIG_META_RF_PRINCETON_TE_US: "RF_PRINCETON_TE_US",
            SIG_META_RF_PRINCETON_GUARD: "RF_PRINCETON_GUARD",
            SIG_META_RF_PRINCETON_BITS: "RF_PRINCETON_BITS",
        }.get(tlv_type, f"TYPE_{tlv_type}")

        value_le = None
        if tlv_len in (1, 2, 4):
            value_le = int.from_bytes(value, "little")

        decoded_value = None
        if tlv_type == SIG_META_RF_MODULATION and tlv_len == 1:
            decoded_value = {
                SIG_RF_MOD_AM270: "AM270",
                SIG_RF_MOD_AM650: "AM650",
            }.get(value[0], f"UNKNOWN_MOD({value[0]})")
        elif tlv_type == SIG_META_RF_FREQ_HZ and tlv_len == 4:
            hz = int.from_bytes(value, "little")
            decoded_value = f"{hz / 1_000_000:.3f} MHz"
        elif tlv_type == SIG_META_RF_PRINCETON_KEY and tlv_len == 4:
            key = int.from_bytes(value, "little")
            decoded_value = f"0x{key:06X}"
        elif tlv_type == SIG_META_RF_PRINCETON_TE_US and tlv_len == 2:
            te = int.from_bytes(value, "little")
            decoded_value = f"{te} us"
        elif tlv_type == SIG_META_RF_PRINCETON_GUARD and tlv_len == 1:
            decoded_value = f"Te*{value[0]}"
        elif tlv_type == SIG_META_RF_PRINCETON_BITS and tlv_len == 1:
            decoded_value = f"{value[0]} bits"

        items.append(
            {
                "type": tlv_type,
                "name": decoded_name,
                "len": tlv_len,
                "value_hex": value.hex(" "),
                "value_le": value_le,
                "decoded_value": decoded_value,
            }
        )
        i = end
    return items, None


def parse_sig_file(path: Path) -> SigRecord:
    data = path.read_bytes()
    if len(data) < SIG_HEADER_SIZE_BYTES:
        raise ValueError(f"{path}: file too small for SIG header.")

    header = bytearray(data[:SIG_HEADER_SIZE_BYTES])
    if bytes(header[:4]) != SIG_MAGIC:
        raise ValueError(f"{path}: bad magic, expected {SIG_MAGIC!r}.")

    header_for_crc = bytearray(header)
    header_crc_stored = int.from_bytes(header_for_crc[28:32], "little")
    header_for_crc[28:32] = b"\x00\x00\x00\x00"
    header_crc_calc = crc32_block(bytes(header_for_crc))
    if header_crc_stored != header_crc_calc:
        raise ValueError(
            f"{path}: header CRC mismatch stored=0x{header_crc_stored:08x} "
            f"calc=0x{header_crc_calc:08x}."
        )

    signal_type = header[4]
    flags = header[5]
    tick_scale = struct.unpack("<b", bytes([header[6]]))[0]
    edge_count = int.from_bytes(header[8:12], "little")
    data_offset = int.from_bytes(header[12:16], "little")
    meta_offset = int.from_bytes(header[16:20], "little")
    meta_size = int.from_bytes(header[20:24], "little")
    payload_crc_stored = int.from_bytes(header[24:28], "little")

    if signal_type not in (SIG_SIGNAL_TYPE_IR, SIG_SIGNAL_TYPE_RF):
        raise ValueError(f"{path}: unsupported signal type {signal_type}.")
    if data_offset != SIG_HEADER_SIZE_BYTES:
        raise ValueError(f"{path}: invalid dataOffset={data_offset}.")
    if meta_size == 0 and meta_offset != 0:
        raise ValueError(f"{path}: metaOffset must be 0 when metaSize is 0.")
    if meta_size > 0 and meta_offset < data_offset:
        raise ValueError(f"{path}: metaOffset before dataOffset.")

    cursor = data_offset
    edges: list[int] = []
    payload_crc = 0xFFFFFFFF
    encoded_edges_size = 0

    for _ in range(edge_count):
        if cursor + 2 > len(data):
            raise ValueError(f"{path}: truncated while reading edge prefix.")
        short_ticks = int.from_bytes(data[cursor : cursor + 2], "little")
        if short_ticks != SIG_EDGE_EXTENDED_16:
            raw = data[cursor : cursor + 2]
            ticks = short_ticks
            cursor += 2
        else:
            if cursor + 6 > len(data):
                raise ValueError(f"{path}: truncated while reading extended edge.")
            raw = data[cursor : cursor + 6]
            ticks = int.from_bytes(raw[2:6], "little")
            cursor += 6

        edges.append(ticks)
        payload_crc = crc32_update(payload_crc, raw)
        encoded_edges_size += len(raw)

    if meta_size > 0:
        expected_meta_offset = data_offset + encoded_edges_size
        if meta_offset != expected_meta_offset:
            raise ValueError(
                f"{path}: metadata offset mismatch, expected {expected_meta_offset}, "
                f"got {meta_offset}."
            )
    elif meta_offset != 0:
        raise ValueError(f"{path}: metaOffset must be 0 when metaSize is 0.")

    metadata = b""
    if meta_size > 0:
        if cursor != meta_offset:
            raise ValueError(f"{path}: cursor/metaOffset mismatch.")
        if cursor + meta_size > len(data):
            raise ValueError(f"{path}: truncated metadata block.")
        metadata = data[cursor : cursor + meta_size]
        payload_crc = crc32_update(payload_crc, metadata)
        cursor += meta_size

    payload_crc_calc = payload_crc ^ 0xFFFFFFFF
    if payload_crc_calc != payload_crc_stored:
        raise ValueError(
            f"{path}: payload CRC mismatch stored=0x{payload_crc_stored:08x} "
            f"calc=0x{payload_crc_calc:08x}."
        )

    return SigRecord(
        signal_type=signal_type,
        flags=flags,
        tick_scale=tick_scale,
        edge_count=edge_count,
        edges=edges,
        metadata=metadata,
        payload_crc_stored=payload_crc_stored,
        payload_crc_calc=payload_crc_calc,
        header_crc_stored=header_crc_stored,
        header_crc_calc=header_crc_calc,
    )


def tick_scale_factor(tick_scale: int) -> float:
    if tick_scale >= 0:
        return float(1 << tick_scale)
    return 1.0 / float(1 << (-tick_scale))


def build_waveform(record: SigRecord) -> tuple[list[float], list[int]]:
    factor = tick_scale_factor(record.tick_scale)
    t = 0.0
    times = [t]
    level = 1 if (record.flags & SIG_FLAG_START_LEVEL) else 0
    levels = [level]
    for ticks in record.edges:
        t += ticks * factor
        times.append(t)
        level = 1 - level
        levels.append(level)
    return times, levels


def metadata_lines(record: SigRecord) -> list[str]:
    lines = [
        f"Signal type: {signal_type_name(record.signal_type)} ({record.signal_type})",
        f"Flags: 0x{record.flags:02x}",
        f"  START_LEVEL: {'yes' if record.flags & SIG_FLAG_START_LEVEL else 'no'}",
        f"  HAS_METADATA: {'yes' if record.flags & SIG_FLAG_HAS_METADATA else 'no'}",
        f"  HAS_PROFILE: {'yes' if record.flags & SIG_FLAG_HAS_PROFILE else 'no'}",
        f"tickScale: {record.tick_scale} (factor={tick_scale_factor(record.tick_scale):g})",
        f"edgeCount: {record.edge_count}",
        f"metadataSize: {len(record.metadata)}",
        f"headerCRC: 0x{record.header_crc_stored:08x} (ok)",
        f"payloadCRC: 0x{record.payload_crc_stored:08x} (ok)",
    ]

    if record.metadata:
        items, error = parse_tlv_metadata(record.metadata)
        lines.append("")
        lines.append("Metadata TLV:")
        if items:
            for item in items:
                if item["decoded_value"] is not None:
                    lines.append(
                        f"  {item['name']} (t={item['type']}, len={item['len']}): "
                        f"{item['decoded_value']} hex={item['value_hex']}"
                    )
                elif item["value_le"] is None:
                    lines.append(
                        f"  {item['name']} (t={item['type']}, len={item['len']}): "
                        f"hex={item['value_hex']}"
                    )
                else:
                    lines.append(
                        f"  {item['name']} (t={item['type']}, len={item['len']}): "
                        f"le={item['value_le']} hex={item['value_hex']}"
                    )
        if error:
            lines.append(f"  Parse warning: {error}")
            lines.append(f"  Raw metadata: {record.metadata.hex(' ')}")
    return lines


@dataclass
class DecodedBit:
    bit: int  # 0 or 1
    t_start: float  # start time in scaled ticks
    t_end: float  # end time in scaled ticks


def decode_bits_princeton(record: SigRecord) -> list[DecodedBit]:
    """Decode Princeton-encoded bits from edges.

    Each bit is two edges (mark high + space low):
      0 = short mark (~Te) + long space (~2-3*Te)
      1 = long mark (~2-3*Te) + short space (~Te)
    The signal starts low, has noise/gap, then a long preamble mark,
    a sync space, and then data bit pairs.
    """
    factor = tick_scale_factor(record.tick_scale)
    edges = record.edges
    if len(edges) < 6:
        return []

    # Find preamble: the longest high pulse (start_level=0 means odd
    # indices are high durations).
    start_level = 1 if (record.flags & SIG_FLAG_START_LEVEL) else 0
    preamble_idx = -1
    preamble_val = 0
    for i in range(len(edges)):
        level_during = (start_level + i) % 2  # level during this edge
        if level_during == 1 and edges[i] > preamble_val:
            preamble_val = edges[i]
            preamble_idx = i

    if preamble_idx < 0 or preamble_idx + 2 >= len(edges):
        return []

    # After preamble mark: skip the sync space, then read (mark, space) pairs
    data_start = preamble_idx + 2  # skip preamble mark + sync space

    # Collect all data edge durations (raw ticks) to find threshold
    data_raw = edges[data_start:]
    if len(data_raw) < 2:
        return []
    short_val = min(data_raw)
    long_val = max(e for e in data_raw if e < preamble_val // 2)
    threshold = (short_val + long_val) / 2.0

    # Accumulate scaled time up to data_start
    t = sum(edges[j] * factor for j in range(data_start))

    bits: list[DecodedBit] = []
    i = data_start
    while i + 1 < len(edges):
        mark_raw = edges[i]
        space_raw = edges[i + 1]

        # Guard/preamble detection: either pulse much larger than data
        if mark_raw > preamble_val // 2 or space_raw > preamble_val // 2:
            break

        t_start = t
        t += (mark_raw + space_raw) * factor
        t_end = t

        if mark_raw < threshold and space_raw >= threshold:
            bits.append(DecodedBit(0, t_start, t_end))
        elif mark_raw >= threshold and space_raw < threshold:
            bits.append(DecodedBit(1, t_start, t_end))

        i += 2

    return bits


def decode_bits_nec(record: SigRecord) -> list[DecodedBit]:
    """Decode NEC-encoded bits from edges.

    Leader: ~9000 µs mark + ~4500 µs space (raw ticks are in µs).
    Each bit: ~560 µs mark + space (~560 µs = 0, ~1690 µs = 1).
    Stop: ~560 µs mark.
    """
    factor = tick_scale_factor(record.tick_scale)
    edges = record.edges
    if len(edges) < 4:
        return []

    # Find leader: first pulse > 5000 raw ticks (the 9000 µs mark)
    leader_idx = -1
    for i in range(len(edges)):
        if edges[i] > 5000:
            leader_idx = i
            break

    if leader_idx < 0 or leader_idx + 3 >= len(edges):
        return []

    # Data starts after leader mark + leader space
    data_start = leader_idx + 2
    t = sum(edges[j] * factor for j in range(data_start))

    threshold_raw = (560.0 + 1690.0) / 2.0  # ~1125 µs in raw ticks

    bits: list[DecodedBit] = []
    i = data_start
    while i + 1 < len(edges):
        mark_raw = edges[i]
        space_raw = edges[i + 1]

        # Verify mark is roughly 560 µs (raw ticks)
        if abs(mark_raw - 560) > 300:
            break

        t_start = t
        t += (mark_raw + space_raw) * factor
        t_end = t

        if space_raw < threshold_raw:
            bits.append(DecodedBit(0, t_start, t_end))
        else:
            bits.append(DecodedBit(1, t_start, t_end))

        if len(bits) >= 32:
            break

        i += 2

    return bits


def decode_bits(record: SigRecord) -> list[DecodedBit]:
    if record.signal_type == SIG_SIGNAL_TYPE_RF:
        return decode_bits_princeton(record)
    if record.signal_type == SIG_SIGNAL_TYPE_IR:
        return decode_bits_nec(record)
    return []


def plot_record(record: SigRecord, title: str, output: Path | None) -> None:
    times, levels = build_waveform(record)
    lines = metadata_lines(record)

    fig = plt.figure(figsize=(13, 6))
    gs = fig.add_gridspec(1, 2, width_ratios=[2.8, 1.2])
    ax_signal = fig.add_subplot(gs[0, 0])
    ax_meta = fig.add_subplot(gs[0, 1])

    ax_signal.step(times, levels, where="post", linewidth=1.4)
    ax_signal.set_ylim(-0.2, 1.5)
    ax_signal.set_yticks([0, 1])
    ax_signal.set_ylabel("Level")
    ax_signal.set_xlabel("Time (scaled ticks)")
    ax_signal.set_title("Signal waveform")
    ax_signal.grid(False)

    decoded = decode_bits(record)
    for db in decoded:
        color = "#D0D0D0" if db.bit == 1 else "#FFFFFF"
        ax_signal.axvspan(db.t_start, db.t_end, color=color, alpha=0.5)
        t_mid = (db.t_start + db.t_end) / 2.0
        ax_signal.text(
            t_mid,
            1.25,
            str(db.bit),
            ha="center",
            va="center",
            fontsize=7,
            color="red",
            fontweight="bold",
        )

    ax_meta.axis("off")
    ax_meta.text(
        0.0,
        1.0,
        "\n".join(lines),
        va="top",
        ha="left",
        family="monospace",
        fontsize=10,
    )

    fig.suptitle(title)
    fig.tight_layout()
    if output is not None:
        fig.savefig(output, dpi=150)
        print(f"Saved plot to {output}")
    else:
        plt.show()


def main() -> None:
    parser = argparse.ArgumentParser(description="Inspect and plot .sig files.")
    parser.add_argument("input", type=Path, help="Path to .sig file")
    parser.add_argument(
        "--save",
        type=Path,
        default=None,
        help="Save plot to image file instead of opening UI window",
    )
    args = parser.parse_args()

    record = parse_sig_file(args.input)
    print(
        f"Loaded {args.input} | type={signal_type_name(record.signal_type)} "
        f"edges={record.edge_count} metadata={len(record.metadata)}B"
    )
    plot_record(record, title=args.input.name, output=args.save)


if __name__ == "__main__":
    main()
