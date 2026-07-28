"""Tests for the listener's optional shared-secret gate. No UE4 required.

The gate is opt-in: with MCP_BRIDGE_TOKEN unset the listener must behave
exactly as it did before, or every existing caller breaks. These tests pin both
halves of that contract, including a real HTTP round trip against the actual
request handler.

Run: python Plugins/MCPBridge/Content/Python/tests/test_listener_auth.py
"""

import json
import os
import sys
import threading
import urllib.error
import urllib.request
from http.server import HTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PYTHON_ROOT = os.path.dirname(HERE)
if PYTHON_ROOT not in sys.path:
    sys.path.insert(0, PYTHON_ROOT)

from mcp_bridge import listener  # noqa: E402

TEST_PORT = 18211


def _set_token(value):
    if value is None:
        os.environ.pop(listener.TOKEN_ENV_VAR, None)
    else:
        os.environ[listener.TOKEN_ENV_VAR] = value


# --- pure predicate ---------------------------------------------------------


def test_no_token_configured_accepts_anything() -> None:
    _set_token(None)
    assert listener.auth_enabled() is False
    assert listener.token_is_valid("") is True
    assert listener.token_is_valid("anything") is True


def test_configured_token_accepts_only_itself() -> None:
    _set_token("s3cret-value")
    try:
        assert listener.auth_enabled() is True
        assert listener.token_is_valid("s3cret-value") is True
        assert listener.token_is_valid("s3cret-valu") is False
        assert listener.token_is_valid("") is False
        assert listener.token_is_valid("S3CRET-VALUE") is False
    finally:
        _set_token(None)


def test_whitespace_is_tolerated_on_both_sides() -> None:
    _set_token("  padded  ")
    try:
        assert listener.token_is_valid("padded") is True
        assert listener.token_is_valid("  padded  ") is True
    finally:
        _set_token(None)


def test_whitespace_only_token_counts_as_unset() -> None:
    """An env var set to spaces must not silently enable a blank password."""
    _set_token("   ")
    try:
        assert listener.auth_enabled() is False
        assert listener.token_is_valid("whatever") is True
    finally:
        _set_token(None)


# --- real HTTP round trip ---------------------------------------------------


def _serve(server):
    server.serve_forever(poll_interval=0.05)


def _post(token=None, timeout=5.0):
    # Deliberately omits "command". A request that clears the auth gate then
    # fails validation with 400 immediately, whereas a real command would be
    # queued for a game thread that does not exist in a test process and would
    # block for the full 300s command timeout. 401 vs 400 cleanly separates
    # "blocked at the door" from "got inside".
    body = json.dumps({"params": {}}).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    if token is not None:
        headers[listener.TOKEN_HEADER] = token
    request = urllib.request.Request(
        f"http://127.0.0.1:{TEST_PORT}/", data=body, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read().decode("utf-8")


def _with_server(fn):
    server = HTTPServer(("127.0.0.1", TEST_PORT), listener.BridgeRequestHandler)
    thread = threading.Thread(target=_serve, args=(server,), daemon=True)
    thread.start()
    try:
        fn()
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5.0)


def test_http_rejects_a_missing_token_when_configured() -> None:
    _set_token("live-token")
    try:
        def check():
            status, body = _post(token=None)
            assert status == 401, f"expected 401, got {status}: {body}"
            # The response must not reveal whether the token was absent or wrong.
            assert "live-token" not in body, "response leaked the secret"
        _with_server(check)
    finally:
        _set_token(None)


def test_http_rejects_a_wrong_token() -> None:
    _set_token("live-token")
    try:
        def check():
            status, _ = _post(token="wrong-token")
            assert status == 401, f"expected 401, got {status}"
        _with_server(check)
    finally:
        _set_token(None)


def test_http_accepts_the_right_token() -> None:
    """A correct token gets past the gate and reaches command validation."""
    _set_token("live-token")
    try:
        def check():
            status, body = _post(token="live-token")
            assert status != 401, f"correct token was rejected: {body}"
            assert status == 400, f"expected to reach command validation, got {status}"
        _with_server(check)
    finally:
        _set_token(None)


def test_http_unauthenticated_request_passes_when_no_token_set() -> None:
    """The compatibility guarantee: unset token means nothing changes."""
    _set_token(None)

    def check():
        status, body = _post(token=None)
        assert status != 401, f"gate fired with no token configured: {body}"
        assert status == 400, f"expected to reach command validation, got {status}"
    _with_server(check)


def main() -> int:
    tests = [
        test_no_token_configured_accepts_anything,
        test_configured_token_accepts_only_itself,
        test_whitespace_is_tolerated_on_both_sides,
        test_whitespace_only_token_counts_as_unset,
        test_http_rejects_a_missing_token_when_configured,
        test_http_rejects_a_wrong_token,
        test_http_accepts_the_right_token,
        test_http_unauthenticated_request_passes_when_no_token_set,
    ]
    failed = 0
    for fn in tests:
        try:
            fn()
            print(f"  PASS  {fn.__name__}")
        except AssertionError as exc:
            failed += 1
            print(f"  FAIL  {fn.__name__}: {exc}")
        except Exception as exc:  # noqa: BLE001
            failed += 1
            print(f"  FAIL  {fn.__name__}: unexpected {type(exc).__name__}: {exc}")
    print(f"\n{len(tests) - failed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
