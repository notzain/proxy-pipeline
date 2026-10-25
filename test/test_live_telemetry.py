import json
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import urlopen
from unittest.mock import ANY, Mock, patch
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import chain_runner
from proxy_runner.topology import project_chain_topology, publish_topology
from read_telemetry import (
    MAX_TIMING_SAMPLES_PER_METRIC,
    TelemetryCollector,
    create_web_server,
)


class TopologyProjectionTests(unittest.TestCase):
    def test_projects_channel_edges_and_static_route_targets_without_config(self):
        chain = {
            "backend": "shm",
            "config": {"telemetry.prefix": "proxy", "secret": "not-public"},
            "handlers": [
                {
                    "name": "ingress",
                    "type": "udp_ingress_handler",
                    "output_channel": "raw",
                    "config": {"ingress.port": 9123},
                },
                {
                    "name": "router",
                    "type": "route_handler",
                    "input_channel": "raw",
                    "output_channel": "default",
                    "config": {"route.rule.vip.target": "priority"},
                },
                {
                    "name": "priority_sink",
                    "type": "print_handler",
                    "input_channel": "priority",
                },
            ],
        }

        topology = project_chain_topology(chain, "orders")

        self.assertEqual(topology["chainId"], "orders")
        self.assertEqual(topology["backend"], "shm")
        self.assertEqual(topology["nodes"][1]["metricPrefix"], "proxy.router")
        self.assertEqual(
            topology["edges"],
            [
                {"from": "ingress", "to": "router", "channel": "raw"},
                {"from": "router", "to": "priority_sink", "channel": "priority"},
            ],
        )
        self.assertEqual(topology["unresolvedOutputChannels"], ["default"])
        self.assertNotIn("config", topology)
        self.assertNotIn("secret", json.dumps(topology))

    def test_rejects_chains_without_handlers(self):
        with self.assertRaisesRegex(ValueError, "non-empty handlers"):
            project_chain_topology({"backend": "shm", "handlers": []}, "empty")


class TelemetryCollectorTests(unittest.TestCase):
    def setUp(self):
        self.collector = TelemetryCollector()

    def test_parses_cumulative_counters_and_gauges(self):
        self.collector.parse_packet(
            "proxy.ingress.messages_ingress_total:41|c\n"
            "proxy.ingress.queue_depth:7|g\n"
        )

        counters, gauges, _ = self.collector.get_snapshot()
        self.assertEqual(counters["proxy.ingress.messages_ingress_total"], 41)
        self.assertEqual(gauges["proxy.ingress.queue_depth"], 7)
        self.assertIsNotNone(self.collector.get_api_snapshot()["lastReceivedAt"])

    def test_peak_throughput_tracks_source_counter_rate(self):
        topology = project_chain_topology({
            "backend": "shm",
            "handlers": [{
                "name": "ingress",
                "type": "udp_ingress_handler",
                "output_channel": "raw",
            }],
        }, "orders")
        self.collector.set_topology(topology)

        with patch("read_telemetry.time.time", side_effect=[1.0, 3.0, 3.0, 4.0]):
            self.collector.parse_packet("proxy.ingress.messages_ingress_total:100|c")
            self.collector.parse_packet("proxy.ingress.messages_ingress_total:120|c")
            self.assertEqual(self.collector.get_api_snapshot()["peakThroughput"], 10.0)
            self.collector.parse_packet("proxy.ingress.messages_ingress_total:150|c")

        snapshot = self.collector.get_api_snapshot()
        self.assertEqual(snapshot["peakThroughput"], 30.0)

    def test_peak_throughput_resets_when_topology_identity_changes(self):
        topology = project_chain_topology({
            "backend": "shm",
            "handlers": [{
                "name": "ingress",
                "type": "udp_ingress_handler",
                "output_channel": "raw",
            }],
        }, "orders")
        self.collector.set_topology(topology)
        with patch("read_telemetry.time.time", side_effect=[1.0, 2.0]):
            self.collector.parse_packet("proxy.ingress.messages_ingress_total:10|c")
            self.collector.parse_packet("proxy.ingress.messages_ingress_total:30|c")
        self.assertEqual(self.collector.get_api_snapshot()["peakThroughput"], 20.0)

        changed_topology = dict(topology, chainId="payments")
        self.collector.set_topology(changed_topology)
        self.assertEqual(self.collector.get_api_snapshot()["peakThroughput"], 0.0)

    def test_invalid_values_do_not_mark_metrics_as_live(self):
        self.collector.parse_packet("proxy.ingress.queue_depth:nan|g\n")

        snapshot = self.collector.get_api_snapshot()
        self.assertIsNone(snapshot["lastReceivedAt"])
        self.assertEqual(snapshot["gauges"], {})

    def test_timing_sample_retention_is_bounded(self):
        samples = "\n".join(f"proxy.ingress.sample:{value}|ms" for value in range(MAX_TIMING_SAMPLES_PER_METRIC + 20))
        self.collector.parse_packet(samples)

        _, _, timings = self.collector.get_snapshot()
        self.assertEqual(len(timings["proxy.ingress.sample"]), MAX_TIMING_SAMPLES_PER_METRIC)
        self.assertEqual(timings["proxy.ingress.sample"][0], 20)

    def test_topology_validation_drops_unapproved_fields(self):
        self.collector.set_topology({
            "chainId": "orders",
            "backend": "shm",
            "nodes": [{
                "name": "ingress",
                "type": "udp_ingress_handler",
                "metricPrefix": "proxy.ingress",
                "inputChannels": [],
                "outputChannels": ["raw"],
                "config": {"secret": "hidden"},
            }],
            "edges": [],
            "unresolvedOutputChannels": ["raw"],
            "secrets": ["hidden"],
        })

        topology = self.collector.get_topology()
        self.assertNotIn("secrets", topology)
        self.assertNotIn("config", topology["nodes"][0])

    def test_topology_rejects_edges_to_unknown_nodes(self):
        with self.assertRaisesRegex(ValueError, "unknown node"):
            self.collector.set_topology({
                "chainId": "orders",
                "backend": "shm",
                "nodes": [{
                    "name": "ingress",
                    "type": "udp_ingress_handler",
                    "metricPrefix": "proxy.ingress",
                    "inputChannels": [],
                    "outputChannels": ["raw"],
                }],
                "edges": [{"from": "ingress", "to": "missing", "channel": "raw"}],
            })

    def test_topology_rejects_non_string_edge_endpoints(self):
        with self.assertRaisesRegex(ValueError, "endpoints"):
            self.collector.set_topology({
                "chainId": "orders",
                "backend": "shm",
                "nodes": [{
                    "name": "ingress",
                    "type": "udp_ingress_handler",
                    "metricPrefix": "proxy.ingress",
                }],
                "edges": [{"from": [], "to": "ingress", "channel": "raw"}],
            })

    def test_http_api_serves_metrics_and_published_topology(self):
        server = create_web_server(self.collector, 0, "127.0.0.1")
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base_url = f"http://127.0.0.1:{server.server_address[1]}"
        try:
            with self.assertRaises(HTTPError) as missing_topology:
                urlopen(f"{base_url}/api/topology")
            self.assertEqual(missing_topology.exception.code, 404)
            missing_topology.exception.close()
            with urlopen(f"{base_url}/api/metrics") as response:
                self.assertFalse(json.load(response)["topologyAvailable"])

            topology = project_chain_topology({
                "backend": "file",
                "handlers": [{
                    "name": "ingress",
                    "type": "udp_ingress_handler",
                    "output_channel": "raw",
                }],
            }, "orders")
            publish_topology(base_url, topology)

            with urlopen(f"{base_url}/api/topology") as response:
                self.assertEqual(json.load(response)["chainId"], "orders")
            with urlopen(f"{base_url}/api/metrics") as response:
                metrics = json.load(response)
                self.assertIn("lastReceivedAt", metrics)
                self.assertEqual(metrics["peakThroughput"], 0.0)
                self.assertTrue(metrics["topologyAvailable"])
            with urlopen(f"{base_url}/telemetry_dashboard.css") as response:
                self.assertIn("text/css", response.headers["Content-Type"])
                self.assertIn(b".bg-sky-500", response.read())
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)


class ChainRunnerTopologyRetryTests(unittest.TestCase):
    def test_process_group_signal_skips_exited_process(self):
        process = SimpleNamespace(pid=12345, poll=lambda: 0, send_signal=Mock())
        with patch.object(chain_runner.os, "killpg") as killpg:
            chain_runner._signal_process_group(process, chain_runner.signal.SIGINT)

        killpg.assert_not_called()
        process.send_signal.assert_not_called()

    def test_process_group_permission_race_falls_back_to_live_child(self):
        process = SimpleNamespace(pid=12345, poll=lambda: None, send_signal=Mock())
        with patch.object(chain_runner.os, "killpg", side_effect=PermissionError):
            chain_runner._signal_process_group(process, chain_runner.signal.SIGINT)

        process.send_signal.assert_called_once_with(chain_runner.signal.SIGINT)

    def test_retries_topology_publication_while_chain_is_running(self):
        class FakeProcess:
            pid = 12345

            def __init__(self):
                self.poll_calls = 0

            def poll(self):
                self.poll_calls += 1
                return 0 if self.poll_calls > 6 else None

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            bin_dir = temp_path / "bin"
            bin_dir.mkdir()
            runtime_dir = temp_path / "runtime"
            runtime_dir.mkdir()
            (bin_dir / "worker_file").touch()
            fake_process = FakeProcess()
            monotonic_time = iter(range(100))
            chain_config = {
                "backend": "file",
                "handlers": [{"name": "worker", "type": "worker"}],
                "config": {},
            }
            output = StringIO()
            with redirect_stdout(output):
                with (
                    patch.object(chain_runner, "load_chain_config", return_value=chain_config),
                    patch.object(chain_runner, "find_bin_dir", return_value=bin_dir),
                    patch.object(chain_runner.tempfile, "mkdtemp", return_value=str(runtime_dir)),
                    patch.object(chain_runner, "load_handler_descriptor", return_value=SimpleNamespace(is_external_ingress=True)),
                    patch.object(chain_runner, "generate_handler_config", return_value=""),
                    patch.object(chain_runner.subprocess, "Popen", return_value=fake_process) as popen,
                    patch.object(chain_runner.signal, "signal") as register_signal,
                    patch.object(chain_runner.os, "killpg") as killpg,
                    patch.object(chain_runner.time, "sleep"),
                    patch.object(chain_runner.time, "monotonic", side_effect=lambda: next(monotonic_time)),
                    patch.object(chain_runner, "project_chain_topology", return_value={"chainId": "orders"}),
                    patch.object(
                        chain_runner,
                        "publish_topology",
                        side_effect=[RuntimeError("collector unavailable"), None],
                    ) as publish,
                ):
                    chain_runner.run_chain(
                        Path("orders.toml"),
                        dashboard_port=None,
                        telemetry_url="http://127.0.0.1:8126",
                    )

            self.assertEqual(publish.call_count, 2)
            self.assertFalse(runtime_dir.exists())
            self.assertTrue(popen.call_args.kwargs["start_new_session"])
            register_signal.assert_any_call(chain_runner.signal.SIGHUP, ANY)
            killpg.assert_not_called()
            self.assertIn("Live chain running. Waiting for input", output.getvalue())
            self.assertNotIn("Packets are paused", output.getvalue())


if __name__ == "__main__":
    unittest.main()
