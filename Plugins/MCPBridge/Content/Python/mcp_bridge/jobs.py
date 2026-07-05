"""Asynchronous job system for long-running subprocess work (UBT builds,
cooks, packaging).

Jobs run subprocesses on daemon worker threads and never touch the unreal
module, so they are safe off the game thread. Handlers (game thread) only
start jobs and read the table. The table is process-local and disposable;
it does not survive an editor restart.
"""

import subprocess
import threading
import time
import uuid
from collections import deque
from typing import Any, Callable, Dict, List, Optional

# Keep the last N output lines per job to bound memory.
MAX_OUTPUT_LINES = 400

_jobs: Dict[str, Dict[str, Any]] = {}
_jobs_lock = threading.Lock()

# Kinds that must not run concurrently with themselves (destructive or
# mutually exclusive by nature, e.g. two UBT builds fight over outputs).
EXCLUSIVE_KINDS = {"cpp_build", "cook", "package"}


def _now() -> float:
    return time.time()


def _public_view(job: Dict[str, Any], include_output: bool = False) -> Dict[str, Any]:
    view = {
        "job_id": job["id"],
        "kind": job["kind"],
        "description": job["description"],
        "status": job["status"],
        "started_at": job["started_at"],
        "finished_at": job["finished_at"],
        "duration_sec": round((job["finished_at"] or _now()) - job["started_at"], 1),
        "returncode": job["returncode"],
        "error": job["error"],
        "result": job["result"],
        "output_lines": len(job["output"]),
    }
    if include_output:
        view["output_tail"] = list(job["output"])
    return view


def start_subprocess_job(
    kind: str,
    description: str,
    argv: List[str],
    cwd: Optional[str] = None,
    timeout_seconds: float = 1800.0,
    parse_result: Optional[Callable[[List[str], int], Dict[str, Any]]] = None,
) -> Dict[str, Any]:
    """Start a subprocess as a background job.

    Args:
        kind: Job category (cpp_build, cook, ...). Exclusive kinds reject a
            second concurrent job of the same kind.
        description: Human-readable description for job listings.
        argv: Command line as a list (no shell).
        cwd: Working directory.
        timeout_seconds: Hard kill deadline.
        parse_result: Optional callable(output_lines, returncode) -> dict
            producing a structured result (e.g. parsed compiler errors).

    Returns:
        {success, data: {job_id, ...}} envelope.
    """
    with _jobs_lock:
        if kind in EXCLUSIVE_KINDS:
            for job in _jobs.values():
                if job["kind"] == kind and job["status"] == "running":
                    return {
                        "success": False,
                        "data": {"conflicting_job_id": job["id"]},
                        "error": f"A '{kind}' job is already running ({job['id']}). "
                                 f"Wait for it or cancel it first.",
                    }
        job_id = uuid.uuid4().hex[:12]
        job: Dict[str, Any] = {
            "id": job_id,
            "kind": kind,
            "description": description,
            "argv": argv,
            "status": "running",
            "started_at": _now(),
            "finished_at": None,
            "returncode": None,
            "output": deque(maxlen=MAX_OUTPUT_LINES),
            "error": None,
            "result": None,
            "cancel_requested": False,
            "process": None,
        }
        _jobs[job_id] = job

    def _worker() -> None:
        proc = None
        try:
            proc = subprocess.Popen(
                argv,
                cwd=cwd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                errors="replace",
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            with _jobs_lock:
                job["process"] = proc

            deadline = _now() + timeout_seconds
            assert proc.stdout is not None
            for line in proc.stdout:
                job["output"].append(line.rstrip("\n"))
                if job["cancel_requested"]:
                    proc.terminate()
                    break
                if _now() > deadline:
                    proc.kill()
                    with _jobs_lock:
                        job["error"] = f"Job exceeded timeout of {int(timeout_seconds)}s and was killed"
                    break
            returncode = proc.wait(timeout=30)

            with _jobs_lock:
                job["returncode"] = returncode
                job["finished_at"] = _now()
                if job["cancel_requested"]:
                    job["status"] = "cancelled"
                    job["error"] = job["error"] or "Cancelled by request"
                elif job["error"]:  # timeout path
                    job["status"] = "failed"
                elif returncode == 0:
                    job["status"] = "succeeded"
                else:
                    job["status"] = "failed"
                    job["error"] = f"Process exited with code {returncode}"

            if parse_result is not None:
                try:
                    parsed = parse_result(list(job["output"]), returncode)
                except Exception as exc:
                    parsed = {"parse_error": str(exc)}
                with _jobs_lock:
                    job["result"] = parsed

        except Exception as exc:
            with _jobs_lock:
                job["status"] = "failed"
                job["error"] = f"Job worker error: {exc}"
                job["finished_at"] = _now()
                if proc is not None and proc.poll() is None:
                    try:
                        proc.kill()
                    except Exception:
                        pass

    thread = threading.Thread(target=_worker, name=f"MCPJob-{kind}-{job_id}", daemon=True)
    thread.start()

    return {"success": True, "data": {"job_id": job_id, "kind": kind, "status": "running",
                                      "note": "Poll job_status with this job_id."}}


def get_job(job_id: str, include_output: bool = True) -> Dict[str, Any]:
    """Fetch one job's status and output tail."""
    with _jobs_lock:
        job = _jobs.get(job_id)
        if job is None:
            return {"success": False, "data": {},
                    "error": f"Unknown job_id: {job_id}. Jobs do not survive editor restarts."}
        return {"success": True, "data": _public_view(job, include_output=include_output)}


def list_jobs() -> Dict[str, Any]:
    """List all jobs in this editor session, newest first."""
    with _jobs_lock:
        views = [_public_view(j) for j in _jobs.values()]
    views.sort(key=lambda v: v["started_at"], reverse=True)
    return {"success": True, "data": {"jobs": views, "count": len(views)}}


def cancel_job(job_id: str) -> Dict[str, Any]:
    """Request cancellation of a running job (terminates the subprocess)."""
    with _jobs_lock:
        job = _jobs.get(job_id)
        if job is None:
            return {"success": False, "data": {}, "error": f"Unknown job_id: {job_id}"}
        if job["status"] != "running":
            return {"success": False, "data": {"status": job["status"]},
                    "error": f"Job is not running (status: {job['status']})"}
        job["cancel_requested"] = True
        proc = job.get("process")
    if proc is not None:
        try:
            proc.terminate()
        except Exception:
            pass
    return {"success": True, "data": {"job_id": job_id, "status": "cancel_requested"}}
