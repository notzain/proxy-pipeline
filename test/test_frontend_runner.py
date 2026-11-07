"""
test_frontend_runner.py - Unit test suite for the unified Frontend Studio & Control Plane.
"""

import http.server
import json
from pathlib import Path
import sys
import threading
import tempfile
import time
import unittest
from urllib.error import HTTPError
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

    def test_chain_api_loads_requested_chain_inside_chains_directory(self):
        with urlopen(f"{self.base_url}/api/chain?path=chains%2Fexample_file_chain.toml") as resp:
            self.assertEqual(resp.status, 200)
            chain = json.loads(resp.read().decode("utf-8"))
            self.assertTrue(chain["handlers"])

        for path in ("../README.md", "/etc/passwd"):
            with self.assertRaises(HTTPError) as context:
                urlopen(f"{self.base_url}/api/chain?path={path}")
            self.assertEqual(context.exception.code, 400)
            context.exception.close()

    def test_chain_api_reports_missing_requested_chain(self):
        with self.assertRaises(HTTPError) as context:
            urlopen(f"{self.base_url}/api/chain?path=chains%2Fmissing.toml")
        self.assertEqual(context.exception.code, 404)
        context.exception.close()

    def test_chain_save_preserves_chain_path_and_rejects_traversal(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "chains", prefix="ux_test_") as temp_dir:
            relative_dir = Path(temp_dir).relative_to(ROOT)
            chain_path = relative_dir / "saved.toml"
            (ROOT / chain_path).write_text("backend = \"file\"\n", encoding="utf-8")
            payload = {
                "chainPath": str(chain_path),
                "name": "saved_chain",
                "backend": "sim",
                "config": {},
                "handlers": [],
            }
            req = Request(
                f"{self.base_url}/api/chain/save",
                data=json.dumps(payload).encode("utf-8"),
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            with self.assertRaises(HTTPError) as context:
                urlopen(req)
            self.assertEqual(context.exception.code, 409)
            context.exception.close()

            payload["overwrite"] = True
            req = Request(
                f"{self.base_url}/api/chain/save",
                data=json.dumps(payload).encode("utf-8"),
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            with urlopen(req) as resp:
                self.assertEqual(resp.status, 200)
                self.assertEqual(json.loads(resp.read().decode("utf-8"))["path"], str(chain_path))
            self.assertIn("saved_chain", (ROOT / chain_path).read_text(encoding="utf-8"))

        payload["chainPath"] = "../outside.toml"
        req = Request(
            f"{self.base_url}/api/chain/save",
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with self.assertRaises(HTTPError) as context:
            urlopen(req)
        self.assertEqual(context.exception.code, 400)
        context.exception.close()

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

    def test_simulation_trigger_generator_dynamic_payload(self):
        # Attach a mock coordinator with a generator handler
        chain_cfg = {
            "backend": "sim",
            "handlers": [
                {
                    "name": "sender_1",
                    "type": "dummy_send",
                    "output_channel": "out_test",
                    "config": {"dummy.message": "Static Default Message"},
                }
            ],
        }
        from proxy_runner.coordinator import InteractiveSimulationCoordinator
        coord = InteractiveSimulationCoordinator(chain_cfg, [])
        self.manager.coordinator = coord

        try:
            # Test default payload trigger
            req_default = Request(
                f"{self.base_url}/api/simulation/trigger",
                data=json.dumps({"node": "sender_1"}).encode("utf-8"),
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            with urlopen(req_default) as resp:
                self.assertEqual(resp.status, 200)
                data = json.loads(resp.read().decode("utf-8"))
                self.assertEqual(data["status"], "ok")
                self.assertIn("Static Default Message", data["packet"]["payloadFull"])

            # Reset coordinator
            coord.close()
            coord = InteractiveSimulationCoordinator(chain_cfg, [])
            self.manager.coordinator = coord

            # Test custom dynamic payload trigger
            custom_msg = "<order id='42'><symbol>GOOG</symbol></order>"
            req_custom = Request(
                f"{self.base_url}/api/simulation/trigger",
                data=json.dumps({"node": "sender_1", "payload": custom_msg}).encode("utf-8"),
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            with urlopen(req_custom) as resp:
                self.assertEqual(resp.status, 200)
                data = json.loads(resp.read().decode("utf-8"))
                self.assertEqual(data["status"], "ok")
                self.assertEqual(data["packet"]["payloadFull"], custom_msg)
                self.assertEqual(data["event"]["payloadFull"], custom_msg)
        finally:
            coord.close()
            self.manager.coordinator = None


if __name__ == "__main__":
    unittest.main()
