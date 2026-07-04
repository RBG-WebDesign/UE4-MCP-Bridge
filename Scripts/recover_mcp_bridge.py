"""Recover the MCP Bridge listener in a running UE4 editor.

This uses UE4's Python remote execution channel, so it can restart the bridge
when localhost:8080 is down and normal HTTP commands cannot reach the editor.
Run with UE4's bundled Python 3.7, or use Scripts/recover_mcp_bridge.bat.
"""

import os
import sys
import time


PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PROJECT_PYTHON = os.path.join(PROJECT_ROOT, "Content", "Python")
DEFAULT_ENGINE_ROOT = r"D:\UE\UE_4.27\Engine"


def _engine_root():
    return os.environ.get("UE_ENGINE_ROOT", DEFAULT_ENGINE_ROOT)


def _remote_execution_path():
    return os.path.join(
        _engine_root(),
        "Plugins",
        "Experimental",
        "PythonScriptPlugin",
        "Content",
        "Python",
    )


def _project_root_normalized(path):
    return os.path.normcase(os.path.abspath(path).replace("\\", "/").rstrip("/"))


def _select_node(nodes):
    desired = _project_root_normalized(PROJECT_ROOT)
    for node in nodes:
        project_root = node.get("project_root", "")
        if _project_root_normalized(project_root) == desired:
            return node
    return nodes[0] if nodes else None


def main():
    remote_path = _remote_execution_path()
    if remote_path not in sys.path:
        sys.path.insert(0, remote_path)

    try:
        import remote_execution
    except Exception as exc:
        print("Could not import UE remote_execution.py: {}".format(exc))
        print("Expected path: {}".format(remote_path))
        return 2

    remote = remote_execution.RemoteExecution()
    remote.start()
    try:
        time.sleep(2.0)
        node = _select_node(remote.remote_nodes)
        if not node:
            print("No running UE4 Python remote execution node was found.")
            print("Check Project Settings > Plugins > Python > Remote Execution.")
            return 3

        remote.open_command_connection(node["node_id"])
        command = (
            "import sys\n"
            "project_python = r'{}'\n"
            "if project_python not in sys.path:\n"
            "    sys.path.insert(0, project_python)\n"
            "from mcp_bridge.listener import start, is_running\n"
            "was_running = is_running()\n"
            "started = start() if not was_running else False\n"
            "print({{'was_running': was_running, 'started': started, 'is_running': is_running()}})\n"
        ).format(PROJECT_PYTHON.replace("\\", "\\\\"))

        result = remote.run_command(command)
        print(result)
        return 0 if result.get("success") else 4
    finally:
        remote.stop()


if __name__ == "__main__":
    raise SystemExit(main())
