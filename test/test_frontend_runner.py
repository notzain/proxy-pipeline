"""
test_frontend_runner.py - Unit test suite for the unified Frontend Studio & Control Plane.
"""

import http.server
import json
from pathlib import Path
import sys
import threading
import time
import unittest
from urllib.request import urlopen, Request
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from frontend_runner import ChainProcessManager, FrontendStudioHandler
from read_telemetry import TelemetryCollector


class FrontendRunnerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root_dir = ROOT
        cls.collector = TelemetryCollector()
        cls.manager = ChainProcessManager(cls.root_dir)

        FrontendStudioHandler.root_dir = cls.root_dir
        FrontendStudioHandler.manager = cls.manager
        FrontendStudioHandler.collector = cls.collector
        FrontendStudioHandler.http_port = 8189

        cls.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 8189), FrontendStudioHandler)
        cls.server_thread = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.server_thread.start()
        cls.base_url = "http://127.0.0.1:8189"

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()

    def test_serves_landing_page(self):
        with urlopen(f"{self.base_url}/") as resp:
            self.assertEqual(resp.status, 200)
            body = resp.read().decode("utf-8")
            self.assertIn("Proxy Pipeline Studio &amp; Control Plane", body)

    def test_serves_builder_and_dashboard_and_telemetry(self):
        for path, marker in [
            ("/builder/", "Visual Chain Builder"),
            ("/dashboard/", "Simulation"),
            ("/telemetry/", "Proxy Pipeline Telemetry"),
        ]:
            with urlopen(f"{self.base_url}{path}") as resp:
                self.assertEqual(resp.status, 200)
                body = resp.read().decode("utf-8")
                self.assertIn(marker, body)

    def test_runner_status_api(self):
        with urlopen(f"{self.base_url}/api/runner/status") as resp:
            self.assertEqual(resp.status, 200)
            data = json.loads(resp.read().decode("utf-8"))
            self.assertEqual(data["state"], "idle")
            self.assertIsNone(data["pid"])
            self.assertIn("logs", data)

    def test_runner_chains_and_pcaps_api(self):
        with urlopen(f"{self.base_url}/api/runner/chains") as resp:
            self.assertEqual(resp.status, 200)
            chains = json.loads(resp.read().decode("utf-8"))
            self.assertIsInstance(chains, list)
            self.assertTrue(len(chains) > 0)
            self.assertTrue(any(c["name"].endswith(".toml") for c in chains))

        with urlopen(f"{self.base_url}/api/runner/pcaps") as resp:
            self.assertEqual(resp.status, 200)
            pcaps = json.loads(resp.read().decode("utf-8"))
            self.assertIsInstance(pcaps, list)

    def test_schemas_and_metrics_api(self):
        with urlopen(f"{self.base_url}/api/schemas") as resp:
            self.assertEqual(resp.status, 200)
            schemas = json.loads(resp.read().decode("utf-8"))
            self.assertIn("print_handler", schemas)

        with urlopen(f"{self.base_url}/api/metrics") as resp:
            self.assertEqual(resp.status, 200)
            metrics = json.loads(resp.read().decode("utf-8"))
            self.assertIn("counters", metrics)

    def test_topology_get_and_post(self):
        payload = {
            "chainId": "test_chain",
            "backend": "socket",
            "nodes": [
                {
                    "name": "node_a",
                    "type": "dummy_send",
                    "metricPrefix": "proxy.node_a",
                    "inputChannels": [],
                    "outputChannels": ["ch1"],
                }
            ],
            "edges": [],
            "unresolvedOutputChannels": ["ch1"],
        }

        req = Request(
            f"{self.base_url}/api/topology",
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urlopen(req) as resp:
            self.assertEqual(resp.status, 204)

        with urlopen(f"{self.base_url}/api/topology") as resp:
            self.assertEqual(resp.status, 200)
            topo = json.loads(resp.read().decode("utf-8"))
            self.assertEqual(topo["chainId"], "test_chain")

    def test_simulation_idle_state(self):
        with urlopen(f"{self.base_url}/api/simulation") as resp:
            self.assertEqual(resp.status, 200)
            sim = json.loads(resp.read().decode("utf-8"))
            self.assertEqual(sim["mode"], "idle")
            self.assertEqual(sim["packetCount"], 0)


if __name__ == "__main__":
    unittest.main()

