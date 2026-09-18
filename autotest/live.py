"""Single-controller client for an AutoTest interactive mailbox (standard library only)."""

import argparse
import json
import os
from pathlib import Path
import time
import uuid


def read_json(path):
    """Read a published reply, or tolerate a status file being rewritten."""
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (FileNotFoundError, PermissionError, json.JSONDecodeError):
        return None


class LiveClient:
    """Keep one session identity across requests; retry with the same id after a timeout."""

    def __init__(self, output, timeout=90, allow_finished=False):
        self.output = Path(output)
        self.timeout = timeout
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status = read_json(self.output / "status.json")
            if status and status.get("status") == "failed":
                raise RuntimeError(status)
            accepted = ("waiting", "running", "stopping", "passed") if allow_finished else ("waiting",)
            if status and status.get("session") and status.get("status") in accepted:
                self.session = status["session"]
                self.mailbox = Path(status["liveDir"])
                self.last_id = status["lastRequestId"]
                return
            time.sleep(0.05)
        raise TimeoutError("Interactive game did not become ready")

    def send(self, commands, request_id=None, full_state=False):
        """Atomically submit one numbered batch and wait without changing game time."""
        if request_id is None:
            deadline = time.monotonic() + self.timeout
            status = read_json(self.output / "status.json")
            # A reply can become visible just before the following status write completes.
            while status and status.get("session") == self.session and status.get("status") == "running" and time.monotonic() < deadline:
                time.sleep(0.05)
                status = read_json(self.output / "status.json")
            if not status or status.get("session") != self.session or status.get("status") != "waiting":
                raise RuntimeError("Game is not waiting; retry the original --id if its outcome is unknown")
            request_id = status["lastRequestId"] + 1
        if request_id < 1:
            raise ValueError("request id must be positive")
        request = {"session": self.session, "id": request_id,
                   "commands": commands, "fullState": full_state}
        destination = self.mailbox / f"request_{request_id}.json"
        if destination.exists():
            if read_json(destination) != request:
                raise ValueError("This id already belongs to a different request")
        else:
            temporary = destination.with_suffix(f".{uuid.uuid4().hex}.tmp")
            temporary.write_text(json.dumps(request), encoding="utf-8")
            try:
                # Windows rename never overwrites a competing writer's committed request.
                os.rename(temporary, destination)
            finally:
                temporary.unlink(missing_ok=True)
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            response = read_json(self.mailbox / f"response_{request_id}.json")
            if response is not None:
                if response.get("session") != self.session or response.get("id") != request_id:
                    raise RuntimeError("Unexpected reply identity")
                self.last_id = max(self.last_id, request_id)
                return response
            status = read_json(self.output / "status.json")
            if status and status.get("status") == "failed":
                raise RuntimeError(status)
            if status and status.get("session") not in (None, self.session):
                raise RuntimeError("Game session changed; do not replay into another session")
            time.sleep(0.05)
        raise TimeoutError(f"Request {request_id} outcome unknown; retry this same id, not a new one")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", help="build/clang-release/autotest/out/<bootstrap-script>")
    parser.add_argument("commands", nargs="?", default='[{"op":"observe"}]', help="JSON command array")
    parser.add_argument("--file", type=Path, help="read the command array from a UTF-8 JSON file")
    parser.add_argument("--id", type=int, help="reuse the original id after an uncertain request")
    parser.add_argument("--full-state", action="store_true")
    parser.add_argument("--timeout", type=float, default=90)
    args = parser.parse_args()
    commands = json.loads(args.file.read_text(encoding="utf-8") if args.file else args.commands)
    client = LiveClient(args.output, args.timeout, allow_finished=args.id is not None)
    print(json.dumps(client.send(commands, args.id, args.full_state), ensure_ascii=False))


if __name__ == "__main__":
    main()
