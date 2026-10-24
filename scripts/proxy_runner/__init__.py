"""
Proxy runner and simulation package.
Provides modular components for:
- PCAP parsing & packet envelope representation (pcap.py)
- Handler configuration & topology setup (config.py)
- Simulation stepping, hold socket interception & drop detection (coordinator.py)
- Execution trace normalization & diff assertions (trace.py)
- Web visualization & dashboard server (server.py)
"""

from .pcap import PcapPacket, PcapReader
from .config import load_chain_config, find_bin_dir, generate_handler_config
from .coordinator import InteractiveSimulationCoordinator
from .trace import TraceManager, TraceDiffError
from .server import start_dashboard_server

__all__ = [
    "PcapPacket",
    "PcapReader",
    "load_chain_config",
    "find_bin_dir",
    "generate_handler_config",
    "InteractiveSimulationCoordinator",
    "TraceManager",
    "TraceDiffError",
    "start_dashboard_server",
]

