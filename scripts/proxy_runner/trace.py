"""
Trace management: deterministic serialization, normalization, and semantic diff assertions.
Includes source file location mapping (file + line + column) for expected reference artifacts,
and embeds deterministic pipeline topology metadata into exported traces.
"""

import bisect
import json
from pathlib import Path
import re
import sys
from typing import Any, Optional


class TraceDiffError(AssertionError):
    """Raised when an execution trace differs from the expected reference trace."""
    pass


class TraceSourceLocator:
    """
    Parses an expected trace JSON file to extract exact line and column locations
    for every event within the file.
    """

    def __init__(self, file_path: str | Path):
        self.file_path = Path(file_path).resolve()
        self.raw_text = self.file_path.read_text(encoding="utf-8") if self.file_path.is_file() else ""
        self.line_starts = [0]
        for line in self.raw_text.splitlines(keepends=True):
            self.line_starts.append(self.line_starts[-1] + len(line))

        self.event_spans: list[tuple[int, int]] = []
        self._index_events()

    def _index_events(self):
        if not self.raw_text:
            return
        decoder = json.JSONDecoder()
        
        # Check if the document has "events": [...] or is a top-level [...]
        events_match = re.search(r'"events"\s*:\s*\[', self.raw_text)
        if events_match:
            start_bracket = events_match.end() - 1
        else:
            start_bracket = self.raw_text.find("[")

        if start_bracket == -1:
            return

        idx = start_bracket + 1
        length = len(self.raw_text)
        while idx < length:
            while idx < length and self.raw_text[idx] in " \t\r\n,":
                idx += 1
            if idx >= length or self.raw_text[idx] == "]":
                break
            event_start = idx
            try:
                _, end = decoder.raw_decode(self.raw_text, idx)
                self.event_spans.append((event_start, end))
                idx = end
            except Exception:
                break

    def get_line_col(self, byte_offset: int) -> tuple[int, int]:
        """Converts a byte offset to 1-based (line, column)."""
        idx = bisect.bisect_right(self.line_starts, byte_offset) - 1
        line = idx + 1
        col = byte_offset - self.line_starts[idx] + 1
        return line, col

    def get_event_location(self, event_idx: int) -> tuple[int, int]:
        """Returns (line, column) for the start of an event object."""
        if 0 <= event_idx < len(self.event_spans):
            return self.get_line_col(self.event_spans[event_idx][0])
        return 1, 1

    def format_location(self, event_idx: int) -> str:
        """Formats location as standard clickable 'path:line:col' string."""
        line, col = self.get_event_location(event_idx)
        return f"{self.file_path}:{line}:{col}"


class TraceManager:
    """
    Handles normalization, saving, and deterministic diff comparison of simulation traces.
    Volatile timing metrics (timestampNs, latencyUs, eventId) are excluded so that identical
    PCAP executions always yield identical deterministic results.
    """

    COMPARED_FIELDS = (
        "msgId",
        "type",
        "source",
        "target",
        "targetType",
        "channel",
        "status",
        "payloadSummary",
        "payloadFull",
        "batchCount",
        "detail",
    )

    # ANSI terminal color constants
    RESET = "\033[0m"
    BOLD = "\033[1m"
    RED = "\033[31m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    CYAN = "\033[36m"
    GRAY = "\033[90m"

    @classmethod
    def normalize_event(cls, ev: dict[str, Any]) -> dict[str, Any]:
        """Filters an event dict down to semantic fields only."""
        normalized = {}
        for field in cls.COMPARED_FIELDS:
            if field in ev:
                normalized[field] = ev[field]
        if "batchIds" in ev and ev["batchIds"]:
            normalized["batchIds"] = sorted(list(ev["batchIds"]))
        return normalized

    @classmethod
    def normalize_trace(cls, events: list[dict[str, Any]]) -> list[dict[str, Any]]:
        return [cls.normalize_event(ev) for ev in events]

    @classmethod
    def normalize_topology(cls, chain_config: Optional[dict[str, Any]]) -> dict[str, Any]:
        """
        Extracts a clean, deterministic topology representation from chain configuration.
        Sorts handlers and keys so that order is strictly deterministic.
        """
        if not chain_config:
            return {}

        handlers_raw = chain_config.get("handlers", [])
        clean_handlers: list[dict[str, Any]] = []

        for h in handlers_raw:
            entry: dict[str, Any] = {
                "name": str(h.get("name", "")),
                "type": str(h.get("type", "")),
            }
            if "input_channel" in h and h["input_channel"]:
                entry["input_channel"] = str(h["input_channel"])
            if "input_channels" in h and h["input_channels"]:
                chs = h["input_channels"]
                entry["input_channels"] = sorted(list(chs)) if isinstance(chs, list) else [str(chs)]
            if "output_channel" in h and h["output_channel"]:
                entry["output_channel"] = str(h["output_channel"])
            if "output_channels" in h and h["output_channels"]:
                chs = h["output_channels"]
                entry["output_channels"] = sorted(list(chs)) if isinstance(chs, list) else [str(chs)]

            clean_handlers.append(entry)

        # Deterministic sort by handler name
        clean_handlers.sort(key=lambda item: item["name"])

        return {
            "backend": str(chain_config.get("backend", "sim")),
            "handlers": clean_handlers,
        }

    @classmethod
    def save_trace(
        cls,
        events: list[dict[str, Any]],
        out_path: str | Path,
        chain_config: Optional[dict[str, Any]] = None,
    ) -> None:
        """
        Saves a deterministic trace artifact.
        If chain_config is provided, embeds sorted topology metadata alongside events.
        """
        p = Path(out_path)
        p.parent.mkdir(parents=True, exist_ok=True)
        norm_events = cls.normalize_trace(events)

        if chain_config:
            doc = {
                "topology": cls.normalize_topology(chain_config),
                "events": norm_events,
            }
        else:
            doc = {
                "events": norm_events,
            }

        p.write_text(json.dumps(doc, indent=2, sort_keys=True), encoding="utf-8")

    @classmethod
    def load_trace(cls, in_path: str | Path) -> list[dict[str, Any]]:
        """Loads and normalizes the event list from a trace file (supports both object and list root)."""
        p = Path(in_path)
        if not p.is_file():
            raise FileNotFoundError(f"Trace file not found: {in_path}")
        raw = json.loads(p.read_text(encoding="utf-8"))
        if isinstance(raw, dict):
            raw_events = raw.get("events", [])
        elif isinstance(raw, list):
            raw_events = raw
        else:
            raise ValueError(f"Trace file {in_path} must contain a JSON object with 'events' or a list of events")
        return cls.normalize_trace(raw_events)

    @classmethod
    def load_trace_document(cls, in_path: str | Path) -> dict[str, Any]:
        """Loads the complete trace document (topology + events)."""
        p = Path(in_path)
        if not p.is_file():
            raise FileNotFoundError(f"Trace file not found: {in_path}")
        raw = json.loads(p.read_text(encoding="utf-8"))
        if isinstance(raw, dict):
            return {
                "topology": raw.get("topology", {}),
                "events": cls.normalize_trace(raw.get("events", [])),
            }
        elif isinstance(raw, list):
            return {
                "topology": {},
                "events": cls.normalize_trace(raw),
            }
        return {"topology": {}, "events": []}

    @classmethod
    def assert_trace_match(
        cls,
        actual_events: list[dict[str, Any]],
        expected_path: str | Path,
        use_color: bool = True,
    ) -> None:
        """
        Asserts that actual_events match the expected trace file.
        Throws TraceDiffError with rich diagnostics (file:line:col, context window + side-by-side table) on mismatch.
        """
        expected_file = Path(expected_path)
        locator = TraceSourceLocator(expected_file)
        expected_events = cls.load_trace(expected_file)
        actual_norm = cls.normalize_trace(actual_events)

        first_diverge_idx: int | None = None
        mismatched_indices: list[int] = []

        min_len = min(len(actual_norm), len(expected_events))
        for i in range(min_len):
            if actual_norm[i] != expected_events[i]:
                if first_diverge_idx is None:
                    first_diverge_idx = i
                mismatched_indices.append(i)

        if first_diverge_idx is None and len(actual_norm) != len(expected_events):
            first_diverge_idx = min_len

        if first_diverge_idx is not None:
            report = cls._build_diagnostic_report(
                expected_events=expected_events,
                actual_events=actual_norm,
                locator=locator,
                first_idx=first_diverge_idx,
                mismatch_count=len(mismatched_indices) + abs(len(actual_norm) - len(expected_events)),
                use_color=use_color,
            )
            raise TraceDiffError(report)

    @classmethod
    def _build_diagnostic_report(
        cls,
        expected_events: list[dict[str, Any]],
        actual_events: list[dict[str, Any]],
        locator: TraceSourceLocator,
        first_idx: int,
        mismatch_count: int,
        use_color: bool = True,
    ) -> str:
        c_red = cls.RED if use_color else ""
        c_grn = cls.GREEN if use_color else ""
        c_yel = cls.YELLOW if use_color else ""
        c_cyn = cls.CYAN if use_color else ""
        c_gry = cls.GRAY if use_color else ""
        c_bld = cls.BOLD if use_color else ""
        c_rst = cls.RESET if use_color else ""

        exp_first = expected_events[first_idx] if first_idx < len(expected_events) else None
        act_first = actual_events[first_idx] if first_idx < len(actual_events) else None
        target_pkt = (exp_first or act_first or {}).get("msgId", "?")

        diverge_loc = locator.format_location(first_idx)

        lines = [
            "",
            f"{c_red}{c_bld}{'=' * 105}{c_rst}",
            f"{c_red}{c_bld}❌ TRACE DIVERGENCE DETECTED{c_rst}",
            f"{c_red}{c_bld}{'=' * 105}{c_rst}",
            f"{c_bld}Reference File:{c_rst} {locator.file_path}",
            f"{c_bld}First Divergence Location:{c_rst} {c_cyn}{c_bld}{diverge_loc}{c_rst}",
            f"{c_bld}Summary:{c_rst}",
            f"  * Total Expected Events: {c_cyn}{len(expected_events)}{c_rst}  |  Total Actual Events: {c_cyn}{len(actual_events)}{c_rst}",
            f"  * Divergence at Index: {c_yel}[{first_idx}]{c_rst} (Packet #{target_pkt})",
            f"  * Total Mismatched Events: {c_red}{mismatch_count}{c_rst}",
            "",
            f"{c_gry}{'-' * 105}{c_rst}",
            f"{c_cyn}{c_bld}📍 CONTEXT WINDOW: Surrounding Pipeline Flow (Indices [{max(0, first_idx - 2)}] -> [{min(max(len(expected_events), len(actual_events)) - 1, first_idx + 2)}]){c_rst}",
            f"{c_gry}{'-' * 105}{c_rst}",
        ]

        # 1. Context Window with file:line:col per entry
        start_win = max(0, first_idx - 2)
        end_win = min(max(len(expected_events), len(actual_events)), first_idx + 3)

        for idx in range(start_win, end_win):
            exp = expected_events[idx] if idx < len(expected_events) else None
            act = actual_events[idx] if idx < len(actual_events) else None

            is_diverge = (exp != act)
            prefix = f"{c_red}{c_bld}>> [{idx:2d}] 💥 DIVERGE{c_rst}" if is_diverge else f"   [{idx:2d}] {c_grn}  MATCH{c_rst}  "

            ref_ev = exp or act
            pkt_id = ref_ev.get("msgId", "?")
            ev_type = ref_ev.get("type", "unknown")
            target = ref_ev.get("target", "unknown")
            status = ref_ev.get("status", "unknown")

            line, col = locator.get_event_location(idx) if idx < len(expected_events) else (0, 0)
            loc_str = f"{c_gry}(line {line}:{col}){c_rst}" if line > 0 else f"{c_gry}(extra event){c_rst}"

            if is_diverge:
                exp_type = exp.get("type") if exp else "NONE"
                act_type = act.get("type") if act else "NONE"
                exp_target = exp.get("target") if exp else "NONE"
                act_target = act.get("target") if act else "NONE"
                note = f"Expected: {exp_target} ({exp_type}) vs Actual: {act_target} ({act_type})"
                lines.append(f"{prefix} Packet #{pkt_id:<2} {loc_str:<20} | {note}")
            else:
                lines.append(f"{prefix} Packet #{pkt_id:<2} {loc_str:<20} | type: {ev_type:<8} | target: {target:<20} | status: {status}")

        lines.extend([
            "",
            f"{c_gry}{'-' * 105}{c_rst}",
            f"{c_cyn}{c_bld}🔎 DETAILED EVENT COMPARISON: Index [{first_idx}] (Packet #{target_pkt}) -> {diverge_loc}{c_rst}",
            f"{c_gry}{'-' * 105}{c_rst}",
        ])

        # 2. Side-by-Side Comparison Table: Field | Expected | Actual
        col_field_w = 18
        col_val_w = 41

        lines.append(f"┌{'─' * col_field_w}┬{'─' * col_val_w}┬{'─' * col_val_w}┐")
        lines.append(f"│ {'Field':<{col_field_w - 2}} │ {'Expected':<{col_val_w - 2}} │ {'Actual':<{col_val_w - 2}} │")
        lines.append(f"├{'─' * col_field_w}┼{'─' * col_val_w}┼{'─' * col_val_w}┤")

        all_keys = list(cls.COMPARED_FIELDS)
        if exp_first:
            for k in exp_first:
                if k not in all_keys:
                    all_keys.append(k)
        if act_first:
            for k in act_first:
                if k not in all_keys:
                    all_keys.append(k)

        for k in all_keys:
            v_exp = exp_first.get(k) if exp_first else "<Missing Event>"
            v_act = act_first.get(k) if act_first else "<Missing Event>"

            str_exp = str(v_exp)
            str_act = str(v_act)

            if len(str_exp) > col_val_w - 4:
                str_exp = str_exp[: col_val_w - 7] + "..."
            if len(str_act) > col_val_w - 4:
                str_act = str_act[: col_val_w - 7] + "..."

            is_field_mismatch = (v_exp != v_act)
            if is_field_mismatch:
                act_display = f"{c_red}❌ {str_act}{c_rst}"
                pad_len = max(0, col_val_w - 2 - (len(str_act) + 3))
                act_cell = f"{act_display}{' ' * pad_len}"
                exp_cell = f"{str_exp:<{col_val_w - 2}}"
                lines.append(f"│ {c_yel}{k:<{col_field_w - 2}}{c_rst} │ {exp_cell} │ {act_cell} │")
            else:
                lines.append(f"│ {c_gry}{k:<{col_field_w - 2}}{c_rst} │ {str_exp:<{col_val_w - 2}} │ {str_act:<{col_val_w - 2}} │")

        lines.append(f"└{'─' * col_field_w}┴{'─' * col_val_w}┴{'─' * col_val_w}┘")

        # 3. Diagnostic Root-Cause Insight
        insight = cls._derive_insight(exp_first, act_first)
        if insight:
            lines.extend([
                "",
                f"{c_yel}{c_bld}💡 Diagnostic Insight:{c_rst}",
                f"   {insight}",
            ])

        lines.append(f"{c_red}{c_bld}{'=' * 105}{c_rst}\n")
        return "\n".join(lines)

    @classmethod
    def _derive_insight(cls, exp: dict[str, Any] | None, act: dict[str, Any] | None) -> str | None:
        if not exp and act:
            return f"Run produced an unexpected extra event at handler '{act.get('target')}': {act.get('detail')}"
        if exp and not act:
            return f"Run prematurely stopped or dropped packet before handler '{exp.get('target')}' could process it."

        if exp and act:
            exp_type = exp.get("type")
            act_type = act.get("type")
            if exp_type != act_type:
                if act_type == "drop":
                    return f"Handler '{act.get('target')}' unexpectedly DROPPED message #{act.get('msgId')}. Detail: {act.get('detail')}"
                elif exp_type == "drop":
                    return f"Handler '{act.get('target')}' unexpectedly ACCEPTED message #{act.get('msgId')} instead of discarding it."
                else:
                    return f"Message transitioned to '{act_type}' instead of expected '{exp_type}' at handler '{act.get('target')}'."

            if exp.get("status") != act.get("status"):
                return f"Status discrepancy at '{act.get('target')}': expected '{exp.get('status')}' but got '{act.get('status')}'."

            if exp.get("target") != act.get("target"):
                return f"Routing desync: Message routed to '{act.get('target')}' instead of expected '{exp.get('target')}'."

        return None
