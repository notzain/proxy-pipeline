import json
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from proxy_runner.coordinator import InteractiveSimulationCoordinator
from proxy_runner.pcap import PcapPacket


class DatagramQueue:
    def __init__(self, events):
        self.events = list(events)

    def recvfrom(self, _size):
        if not self.events:
            raise BlockingIOError
        return self.events.pop(0).encode("utf-8"), None


class SimulationCoordinatorTests(unittest.TestCase):
    def test_message_results_for_other_handlers_are_buffered_and_correlated(self):
        coordinator = object.__new__(InteractiveSimulationCoordinator)
        coordinator.handlers = [{"name": "alpha"}, {"name": "beta"}]
        coordinator.pending_message_results = []
        coordinator.events_sock = DatagramQueue([
            json.dumps({"type": "message_result", "handler": "beta", "msgTimestampNs": 22}),
            json.dumps({"type": "message_result", "handler": "alpha", "msgTimestampNs": 11}),
        ])

        alpha_result = coordinator._wait_for_message_results(
            "alpha", expected_msg_timestamps={11}, timeout_sec=0.01
        )
        beta_result = coordinator._wait_for_message_results(
            "beta", expected_msg_timestamps={22}, timeout_sec=0.01
        )

        self.assertEqual([result["handler"] for result in alpha_result], ["alpha"])
        self.assertEqual([result["handler"] for result in beta_result], ["beta"])

    def test_missing_result_with_observed_output_is_forwarded(self):
        coordinator = object.__new__(InteractiveSimulationCoordinator)
        coordinator.events = []
        coordinator.event_counter = 1
        coordinator.handlers = []
        coordinator.pending_queue = []
        coordinator.pending_message_results = []
        coordinator._wait_for_message_results = Mock(return_value=[])
        coordinator._poll_held_output = Mock(side_effect=[[b"branch-a"], [b"branch-b"]])
        coordinator._ensure_hold_socket = Mock()

        packet = PcapPacket(1, 123, "127.0.0.1", 0, "127.0.0.1", 0, b"payload", "payload")
        result = coordinator._process_handler_execution(
            handler_spec={
                "name": "splitter",
                "type": "split_handler",
                "output_channels": ["branch-a", "branch-b"],
            },
            source_name="",
            input_channel="input",
            pkt=packet,
            batch_pkts=[packet],
            send_time_ns=0,
            pending_branches=[],
        )

        self.assertEqual(result["event"]["type"], "hop")
        self.assertEqual(result["event"]["result"], "Forwarded")
        self.assertEqual(
            [trace["outputChannel"] for trace in result["event"]["traces"]],
            ["branch-a", "branch-b"],
        )

    def test_downstream_splitter_output_prevents_false_drop(self):
        coordinator = object.__new__(InteractiveSimulationCoordinator)
        coordinator.events = []
        coordinator.event_counter = 1
        coordinator.pending_queue = []
        coordinator.channel_to_consumers = {
            "input": [{
                "name": "splitter",
                "type": "split_handler",
                "output_channels": ["branch-a", "branch-b"],
            }]
        }
        coordinator._wait_for_message_results = Mock(return_value=[])
        coordinator._poll_held_output = Mock(side_effect=[[b"branch-a"], [b"branch-b"]])
        coordinator._ensure_hold_socket = Mock()
        coordinator.active_in_flight = {
            "packet": PcapPacket(1, 123, "127.0.0.1", 0, "127.0.0.1", 0, b"payload", "payload"),
            "batch": [],
            "pending_branches": [],
            "output_channel": "input",
            "held_data": [b"input"],
            "curr_node_name": "sender",
            "curr_node_type": "dummy_send",
            "input_channel": None,
        }

        with patch("proxy_runner.coordinator.socket.socket") as socket_factory:
            socket_factory.return_value.sendto = Mock()
            result = coordinator._advance_in_flight()

        self.assertEqual(result["event"]["type"], "hop")
        self.assertEqual(result["event"]["result"], "Forwarded")
        self.assertEqual(
            [trace["outputChannel"] for trace in result["event"]["traces"]],
            ["branch-a", "branch-b"],
        )

    def test_missing_result_without_evidence_is_not_reported_as_a_drop(self):
        coordinator = object.__new__(InteractiveSimulationCoordinator)
        coordinator.events = []
        coordinator.event_counter = 1
        coordinator.handlers = []
        coordinator.pending_queue = []
        coordinator.pending_message_results = []
        coordinator.active_in_flight = None
        coordinator._wait_for_message_results = Mock(return_value=[])
        coordinator._poll_held_output = Mock(return_value=None)

        packet = PcapPacket(1, 123, "127.0.0.1", 0, "127.0.0.1", 0, b"payload", "payload")
        result = coordinator._process_handler_execution(
            handler_spec={"name": "receiver", "type": "dummy_receive"},
            source_name="",
            input_channel="input",
            pkt=packet,
            batch_pkts=[packet],
            send_time_ns=0,
            pending_branches=[],
        )

        self.assertEqual(result["event"]["type"], "unconfirmed")
        self.assertEqual(result["event"]["result"], "Unknown")
        self.assertNotEqual(result["event"]["type"], "drop")


if __name__ == "__main__":
    unittest.main()
