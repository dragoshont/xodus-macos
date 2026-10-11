"""Bounded incremental Wine lineage admission and control-first package abort."""

from dataclasses import dataclass
from pathlib import Path
import re
import signal
import time


class OwnershipError(RuntimeError):
    pass


@dataclass(frozen=True)
class OwnedProcess:
    generation: tuple
    native_pid: int
    identity: str
    depth: int


class GenerationJournal:
    def __init__(self):
        self.latest, self.records, self.threads = {}, {}, {}
        self.handles, self.pending, self.startup = {}, {}, {}
        self.serial = 0

    def feed(self, line):
        match = re.match(r"^([0-9a-f]+): init_first_thread\( unix_pid=(-?\d+)", line)
        if match:
            tid, native = int(match[1], 16), int(match[2])
            if tid in self.startup:
                raise OwnershipError("Duplicate unpaired startup")
            self.startup[tid] = native
        match = re.match(r"^([0-9a-f]+): init_first_thread\(\) = 0 \{ pid=([0-9a-f]+), tid=([0-9a-f]+)", line)
        if match:
            tid, pid, child = (int(value, 16) for value in match.groups())
            native = self.startup.pop(tid, None)
            if native is None or native <= 0:
                raise OwnershipError("Unpaired successful startup")
            key = self.latest.get(pid)
            if key is None or self.records[key]["native"] is not None:
                self.serial += 1
                key = (pid, self.serial)
                self.records[key] = dict(parent=None, native=None)
                self.latest[pid] = key
            self.records[key]["native"] = native
            self.threads[child] = key
        match = re.match(r"^([0-9a-f]+): new_process\( .*?parent_process=([0-9a-f]+)", line)
        if match:
            tid, handle = (int(value, 16) for value in match.groups())
            owner = self.threads.get(tid)
            parent = owner if handle in (0, 0xffffffff) else self.handles.get((owner, handle))
            if ("process", tid) in self.pending:
                raise OwnershipError("Duplicate pending process")
            self.pending[("process", tid)] = owner, parent
        match = re.match(r"^([0-9a-f]+): new_process\(\) = 0 \{ info=[0-9a-f]+, pid=([0-9a-f]+), handle=([0-9a-f]+)", line)
        if match:
            tid, pid, handle = (int(value, 16) for value in match.groups())
            owner, parent = self.pending.pop(("process", tid), (None, None))
            if owner is None or parent is None:
                raise OwnershipError("Unresolved generation parent")
            self.serial += 1
            key = (pid, self.serial)
            self.records[key] = dict(parent=parent, native=None)
            self.latest[pid] = key
            self.handles[(owner, handle)] = key
        match = re.match(r"^([0-9a-f]+): new_process\(\) = ([0-9a-f]+)", line)
        if match and int(match[2], 16):
            self.pending.pop(("process", int(match[1], 16)), None)
        match = re.match(r"^([0-9a-f]+): new_thread\( process=([0-9a-f]+)", line)
        if match:
            tid, handle = (int(value, 16) for value in match.groups())
            owner = self.threads.get(tid)
            self.pending[("thread", tid)] = owner if handle == 0xffffffff else self.handles.get((owner, handle))
        match = re.match(r"^([0-9a-f]+): new_thread\(\) = 0 \{ tid=([0-9a-f]+)", line)
        if match:
            tid, child = (int(value, 16) for value in match.groups())
            owner = self.pending.pop(("thread", tid), None)
            if owner is None:
                raise OwnershipError("Unresolved generation thread")
            self.threads[child] = owner
        match = re.match(r"^([0-9a-f]+): new_thread\(\) = ([0-9a-f]+)", line)
        if match and int(match[2], 16):
            self.pending.pop(("thread", int(match[1], 16)), None)
        match = re.match(r"^([0-9a-f]+): close_handle\( handle=([0-9a-f]+)", line)
        if match:
            tid, handle = (int(value, 16) for value in match.groups())
            self.handles.pop((self.threads.get(tid), handle), None)

    def descendants(self, root_pid):
        result = []
        for key in self.latest.values():
            native = self.records[key]["native"]
            current, depth, seen = key, 0, set()
            while current is not None:
                if current in seen or current not in self.records:
                    raise OwnershipError("Unproven or cyclic generation")
                seen.add(current)
                record = self.records[current]
                if record["native"] == root_pid:
                    if native is not None:
                        result.append((key, native, depth))
                    break
                current = record["parent"]
                depth += 1
        return result


class IncrementalJournal:
    def __init__(self, path, clock=time.monotonic, line_limit=65536):
        self.path, self.clock = Path(path), clock
        self.journal = GenerationJournal()
        self.offset, self.tail = 0, b""
        self.line_limit = line_limit

    def drain(self, deadline, byte_budget=2_000_000):
        size = self.path.stat().st_size
        if size < self.offset:
            raise OwnershipError("Ownership trace truncated")
        remaining = size - self.offset
        if remaining > byte_budget:
            raise OwnershipError("Ownership backlog exceeds bounded reconciliation")
        with self.path.open("rb") as stream:
            stream.seek(self.offset)
            while remaining:
                if self.clock() >= deadline:
                    raise OwnershipError("Ownership reconciliation deadline")
                block = stream.read(min(65536, remaining))
                if not block:
                    raise OwnershipError("Ownership trace changed during read")
                self.offset += len(block)
                remaining -= len(block)
                lines = (self.tail + block).split(b"\n")
                self.tail = lines.pop()
                if len(self.tail) > self.line_limit or any(len(line) > self.line_limit for line in lines):
                    raise OwnershipError("Ownership trace line exceeds bound")
                for line in lines:
                    if self.clock() >= deadline:
                        raise OwnershipError("Ownership parsing deadline")
                    self.journal.feed(line.decode("ascii", errors="replace"))
        return self.offset

    def settled(self):
        return (self.path.stat().st_size == self.offset and not self.tail and
                not self.journal.startup and not self.journal.pending)


class ControlFirstAbort:
    def __init__(self, reader, root_pid, root_identity, identity, send_signal,
                 clock=time.monotonic, sleep=time.sleep, kill_signal=None):
        self.reader, self.root_pid, self.root_identity = reader, root_pid, root_identity
        self.identity, self.send_signal = identity, send_signal
        self.clock, self.sleep = clock, sleep
        self.kill_signal = kill_signal if kill_signal is not None else getattr(signal, "SIGKILL", None)
        self.admitted = {}
        self.events = []
        self.quiet = False

    def observe(self, deadline):
        self.reader.drain(deadline)
        for generation, pid, depth in self.reader.journal.descendants(self.root_pid):
            if self.clock() >= deadline:
                raise OwnershipError("Identity admission deadline")
            stamp = self.identity(pid, deadline)
            if stamp is None:
                continue
            previous = self.admitted.get(pid)
            if previous and (previous.generation != generation or previous.identity != stamp):
                raise OwnershipError("Native PID generation/identity changed")
            if pid == self.root_pid and stamp != self.root_identity:
                raise OwnershipError("Launcher identity changed")
            self.admitted[pid] = OwnedProcess(generation, pid, stamp, depth)

    def _live(self, deadline):
        live = []
        for item in sorted(self.admitted.values(), key=lambda item: -item.depth):
            if self.clock() >= deadline:
                raise OwnershipError("Emergency identity deadline")
            stamp = self.identity(item.native_pid, deadline)
            if stamp is not None:
                if stamp != item.identity or self.reader.journal.latest.get(item.generation[0]) != item.generation:
                    raise OwnershipError("Uncertain identity/generation; refusing signal")
                live.append(item)
        return live

    def abort(self, seconds=4, grace=.5):
        if self.kill_signal is None:
            raise OwnershipError("Host has no admitted kill signal")
        deadline = self.clock() + seconds
        self.events.append("abort-before-restoration")
        self.observe(deadline)
        settle_end = self.clock() + seconds / 2
        while not self.reader.settled():
            if self.clock() + .05 >= settle_end:
                raise OwnershipError("Incomplete ownership boundary; refusing signal")
            self.sleep(.05)
            self.observe(deadline)
        if not any(item.native_pid == self.root_pid for item in self.admitted.values()):
            raise OwnershipError("Launcher generation not admitted")
        for item in self._live(deadline):
            self.send_signal(item.native_pid, item.identity, signal.SIGTERM, deadline)
            self.events.append(("term", item.native_pid))
        grace_end = min(deadline, self.clock() + grace)
        while self.clock() < grace_end:
            self.observe(deadline)
            if not self._live(deadline):
                break
            self.sleep(min(.02, max(0, grace_end - self.clock())))
        self.observe(deadline)
        for item in self._live(deadline):
            self.send_signal(item.native_pid, item.identity, self.kill_signal, deadline)
            self.events.append(("kill", item.native_pid))
        while self.clock() < deadline:
            self.observe(deadline)
            if not self._live(deadline) and self.reader.settled():
                self.quiet = True
                self.events.append("owned-package-quiescent")
                return
            self.sleep(min(.02, max(0, deadline - self.clock())))
        raise OwnershipError("Owned package did not quiesce within emergency deadline")

    def restore(self, callback):
        if not self.quiet:
            raise OwnershipError("Registry restoration forbidden before owned-package quiescence")
        self.events.append("registry-restoration")
        return callback()
