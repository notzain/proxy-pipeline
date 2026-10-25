"""
Proxy runner and simulation package.
Provides modular components for:
- PCAP parsing & packet envelope representation (pcap.py)
- Handler configuration & topology setup (config.py)
- Simulation stepping, hold socket interception & drop detection (coordinator.py)
- Execution trace normalization & diff assertions (trace.py)
- Web visualization & dashboard server (server.py)
"""

from .handlers import HandlerDescriptor, HandlerProtocol, HandlerRole, load_handler_descriptor
from .injectors import BasePacketInjector, UdpPacketInjector, UnixSocketPacketInjector, get_injector_for_handler
from .pcap import PcapPacket, PcapReader
from .config import load_chain_config, find_bin_dir, generate_handler_config
from .coordinator import InteractiveSimulationCoordinator
from .trace import TraceManager, TraceDiffError
from .server import start_dashboard_server
from .topology import project_chain_topology, publish_topology

__all__ = [
    "PcapPacket",
    "PcapReader",
    "HandlerRole",
    "HandlerProtocol",
    "HandlerDescriptor",
    "load_handler_descriptor",
    "BasePacketInjector",
    "UdpPacketInjector",
    "UnixSocketPacketInjector",
    "get_injector_for_handler",
    "load_chain_config",
    "find_bin_dir",
    "generate_handler_config",
    "InteractiveSimulationCoordinator",
    "TraceManager",
    "TraceDiffError",
    "start_dashboard_server",
    "project_chain_topology",
    "publish_topology",
]
