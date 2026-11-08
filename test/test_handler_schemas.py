"""
test_handler_schemas.py - Unit test suite for decoupled per-handler schemas and validation.
"""

import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from proxy_runner.schema_validator import (
    discover_handler_schemas,
    validate_handler_instance,
    validate_chain_topology,
)
from proxy_runner.config import load_chain_config


class TestHandlerSchemas(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root_dir = Path(__file__).resolve().parent.parent
        cls.schemas = discover_handler_schemas([cls.root_dir / "handler"])

    def test_all_handlers_have_schema(self):
        handler_dirs = [
            d for d in (self.root_dir / "handler").iterdir()
            if d.is_dir() and not d.name.startswith(".")
        ]
        self.assertGreaterEqual(len(handler_dirs), 12)
        for h_dir in handler_dirs:
            schema_file = h_dir / "handler.schema.json"
            self.assertTrue(
                schema_file.is_file(),
                f"Handler '{h_dir.name}' is missing handler.schema.json"
            )

    def test_schema_required_fields(self):
        for name, schema in self.schemas.items():
            self.assertEqual(schema.get("name"), name)
            self.assertIn("role", schema)
            self.assertIn("protocol", schema)
            self.assertIn("ports", schema)
            self.assertIn("inputs", schema["ports"])
            self.assertIn("outputs", schema["ports"])
            self.assertIn("parameters", schema)

    def test_existing_chains_topology_valid(self):
        chains_dir = self.root_dir / "chains"
        for chain_file in chains_dir.glob("*.toml"):
            cfg = load_chain_config(chain_file)
            res = validate_chain_topology(cfg, self.schemas)
            self.assertTrue(
                res["valid"],
                f"Chain '{chain_file.name}' failed validation with errors: {res['errors']}"
            )

    def test_invalid_handler_parameter_detection(self):
        schema = self.schemas.get("xml_verification_handler")
        self.assertIsNotNone(schema)

        # Missing required parameter: xml.xsd_path
        bad_spec = {
            "name": "xml_node",
            "type": "xml_verification_handler",
            "input_channel": "in",
            "output_channel": "out",
            "config": {},
        }
        errors = validate_handler_instance(bad_spec, schema)
        self.assertTrue(any("xml.xsd_path" in e for e in errors))

    def test_duplicate_wire_detection(self):
        # Verify that multiple producers on the same connection wire/channel are flagged as an error
        chain_def = {
            "backend": "sim",
            "handlers": [
                {
                    "name": "sender_1",
                    "type": "dummy_send",
                    "output_channel": "shared_pipe",
                    "config": {"dummy.message": "A", "dummy.delay_ms": 10},
                },
                {
                    "name": "sender_2",
                    "type": "dummy_send",
                    "output_channel": "shared_pipe",
                    "config": {"dummy.message": "B", "dummy.delay_ms": 10},
                },
                {
                    "name": "receiver_1",
                    "type": "dummy_receive",
                    "input_channel": "shared_pipe",
                },
            ],
        }
        res = validate_chain_topology(chain_def, self.schemas)
        self.assertFalse(res["valid"])
        self.assertTrue(any("uniquely named" in err for err in res["errors"]))

    def test_structured_rules_schema_validation(self):
        schema = self.schemas.get("route_handler")
        self.assertIsNotNone(schema)

        # Valid spec with structured rules
        valid_spec = {
            "name": "router",
            "type": "route_handler",
            "input_channel": "in",
            "output_channels": "out1,out2",
            "config": {
                "default_action": "drop",
                "rules": [
                    {
                        "name": "rule1",
                        "type": "expression",
                        "expression": "metadata.price > 100",
                        "action": "forward",
                        "target": "out1",
                    },
                    {
                        "name": "rule2",
                        "type": "lua",
                        "script": "return message.len > 0",
                        "action": "reject",
                    },
                ],
            },
        }
        errors = validate_handler_instance(valid_spec, schema)
        self.assertEqual(errors, [])

        # Invalid spec with bad enum and bad type in array item
        invalid_spec = {
            "name": "router",
            "type": "route_handler",
            "input_channel": "in",
            "output_channels": "out1",
            "config": {
                "default_action": "invalid_action",  # not in enum
                "rules": "not-a-list",  # must be array
            },
        }
        errors = validate_handler_instance(invalid_spec, schema)
        self.assertTrue(any("default_action" in e for e in errors))
        self.assertTrue(any("must be a list/array" in e for e in errors))

    def test_out_of_tree_discovery(self):
        # Verify discovery works dynamically on an arbitrary external directory
        import tempfile
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir) / "custom_handler"
            tmp_path.mkdir()
            custom_schema = {
                "name": "custom_filter",
                "role": "processor",
                "protocol": "none",
                "ports": {"inputs": [], "outputs": []},
                "parameters": {}
            }
            (tmp_path / "handler.schema.json").write_text(json.dumps(custom_schema))

            discovered = discover_handler_schemas([Path(tmp_dir)])
            self.assertIn("custom_filter", discovered)

    def test_embedded_file_and_regex_pattern_validation(self):
        schema = self.schemas.get("xml_verification_handler")
        self.assertIsNotNone(schema)

        # 1. Valid with embedded schema content instead of xsd_path
        valid_embedded_spec = {
            "name": "xml_node",
            "type": "xml_verification_handler",
            "input_channel": "in",
            "output_channel": "out",
            "config": {
                "xml.xsd_content": "<xs:schema></xs:schema>"
            },
        }
        errors = validate_handler_instance(valid_embedded_spec, schema)
        self.assertEqual(errors, [])

        # 2. String pattern regex validation check
        mock_schema_with_regex = {
            "name": "test_node",
            "role": "processor",
            "ports": {"inputs": [{"name": "in"}], "outputs": [{"name": "out"}]},
            "parameters": {
                "endpoint.ip": {
                    "type": "string",
                    "pattern": r"^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$",
                    "required": True,
                },
                "buffer_size": {
                    "type": "integer",
                    "min": 100,
                    "max": 1000,
                }
            }
        }
        bad_regex_spec = {
            "name": "node1",
            "type": "test_node",
            "input_channel": "in",
            "output_channel": "out",
            "config": {
                "endpoint.ip": "not_an_ip",
                "buffer_size": 50,
            }
        }
        regex_errors = validate_handler_instance(bad_regex_spec, mock_schema_with_regex)
        self.assertTrue(any("does not match pattern" in e for e in regex_errors))
        self.assertTrue(any("value 50 < min" in e for e in regex_errors))


if __name__ == "__main__":
    unittest.main()

