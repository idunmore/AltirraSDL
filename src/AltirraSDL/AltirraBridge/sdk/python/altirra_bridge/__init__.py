"""altirra_bridge — Python client for the AltirraBridge protocol.

Connect to a running AltirraSDL launched with --bridge and drive it
programmatically: frame-step, peek/poke memory, capture screenshots,
inject input, run the debugger.

Quick start::

    from altirra_bridge import AltirraBridge

    with AltirraBridge.from_token_file("/tmp/altirra-bridge-12345.token") as a:
        print(a.ping())
        a.frame(60)
        print(a.ping())

See AltirraBridge/docs/PROTOCOL.md for the wire contract and
AltirraBridge/docs/api/python-client.md for the full API reference.
"""

from .client import AltirraBridge, BridgeError, AuthError, RemoteError, RawFrame
from .loader import XexImage, XexSegment, parse_xex, load_xex
from .project import Project, Note
from . import install_skills

__all__ = [
    # Client
    "AltirraBridge", "BridgeError", "AuthError", "RemoteError", "RawFrame",
    # XEX loader
    "XexImage", "XexSegment", "parse_xex", "load_xex",
    # RE project
    "Project", "Note",
    # Skills installer
    "install_skills",
]
__version__ = "0.1.0"
