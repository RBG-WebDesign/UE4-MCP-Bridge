"""Content Browser folder visibility handlers.

Thin adapters over unreal.FolderVisibilityLibrary (MCPBridgeGraphBuilder C++).
Display-only: hidden folders stay on disk, referenced, and cookable. The
hidden list persists in Config/FolderVisibility.ini and is re-applied at
editor startup. Not transactional: this changes editor UI state, not assets.
"""
from typing import Any, Dict

import unreal


def _lib() -> Any:
    """Return the C++ library class, or None when the editor runs old binaries."""
    return getattr(unreal, "FolderVisibilityLibrary", None)


def handle_folder_hide(params: Dict[str, Any]) -> Dict[str, Any]:
    """Hide one or more /Game/... folders in the Content Browser."""
    lib = _lib()
    if lib is None:
        return {"success": False, "data": {},
                "error": "FolderVisibilityLibrary not available; rebuild C++ and restart the editor"}
    folders = params.get("folders") or ([params["folder"]] if params.get("folder") else [])
    if not folders:
        return {"success": False, "data": {}, "error": "folder_hide needs 'folder' or 'folders'"}
    hidden, rejected = [], []
    for f in folders:
        (hidden if lib.hide_folder(str(f)) else rejected).append(str(f))
    return {"success": len(rejected) == 0,
            "data": {"hidden": hidden, "rejected": rejected,
                     "hidden_total": list(lib.get_hidden_folders())},
            "error": None if not rejected else f"Rejected (must be a /Game subfolder): {rejected}"}


def handle_folder_show(params: Dict[str, Any]) -> Dict[str, Any]:
    """Unhide folders. With no params, unhides everything (escape hatch)."""
    lib = _lib()
    if lib is None:
        return {"success": False, "data": {},
                "error": "FolderVisibilityLibrary not available; rebuild C++ and restart the editor"}
    folders = params.get("folders") or ([params["folder"]] if params.get("folder") else [])
    if not folders:
        count = lib.show_all_folders()
        return {"success": True, "data": {"shown_count": int(count), "hidden_total": []}, "error": None}
    shown, missing = [], []
    for f in folders:
        (shown if lib.show_folder(str(f)) else missing).append(str(f))
    return {"success": True,
            "data": {"shown": shown, "not_hidden": missing,
                     "hidden_total": list(lib.get_hidden_folders())},
            "error": None}


def handle_folder_hidden_list(params: Dict[str, Any]) -> Dict[str, Any]:
    """List folders currently hidden by this feature."""
    lib = _lib()
    if lib is None:
        return {"success": False, "data": {},
                "error": "FolderVisibilityLibrary not available; rebuild C++ and restart the editor"}
    return {"success": True, "data": {"hidden": list(lib.get_hidden_folders())}, "error": None}
