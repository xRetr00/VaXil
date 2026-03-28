from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
import json
from typing import List
import uuid


SCHEMA_VERSION = "1.0"
MESSAGE_TYPE = "vision.snapshot"


def utc_timestamp() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


@dataclass(slots=True)
class VisionObject:
    class_name: str
    confidence: float

    def to_wire(self) -> dict:
        return {
            "class": self.class_name,
            "confidence": round(float(self.confidence), 4),
        }


@dataclass(slots=True)
class VisionGesture:
    name: str
    confidence: float

    def to_wire(self) -> dict:
        return {
            "name": self.name,
            "confidence": round(float(self.confidence), 4),
        }


@dataclass(slots=True)
class VisionSnapshotMessage:
    node_id: str
    objects: List[VisionObject] = field(default_factory=list)
    gestures: List[VisionGesture] = field(default_factory=list)
    finger_count: int | None = None
    summary: str = ""
    trace_id: str = field(default_factory=lambda: uuid.uuid4().hex)
    ts: str = field(default_factory=utc_timestamp)
    type: str = MESSAGE_TYPE
    version: str = SCHEMA_VERSION

    def to_wire(self) -> dict:
        return {
            "type": self.type,
            "version": self.version,
            "ts": self.ts,
            "trace_id": self.trace_id,
            "node_id": self.node_id,
            "objects": [item.to_wire() for item in self.objects],
            "gestures": [item.to_wire() for item in self.gestures],
            "finger_count": self.finger_count,
            "summary": self.summary,
        }

    def to_json(self) -> str:
        return json.dumps(self.to_wire(), separators=(",", ":"))
