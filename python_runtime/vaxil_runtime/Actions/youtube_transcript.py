from __future__ import annotations

from .common import failure, success

SPEC = {
    "name": "youtube_transcript",
    "title": "YouTube Transcript",
    "description": "Fetch a transcript using youtube-transcript-api when available.",
    "args_schema": {"type": "object", "properties": {"video_id": {"type": "string"}, "languages": {"type": "array", "items": {"type": "string"}}}, "required": ["video_id"]},
    "risk_level": "network_access",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["youtube"],
}


def run(service, args, _context):
    if service.youtube_transcript_api is None:
        return failure("Transcript unavailable", "youtube-transcript-api is not installed in the runtime.")
    video_id = str(args.get("video_id") or "").strip()
    languages = list(args.get("languages") or ["en"])
    api = getattr(service.youtube_transcript_api, "YouTubeTranscriptApi")
    transcript = api.get_transcript(video_id, languages=languages)
    text = "\n".join(segment.get("text", "").strip() for segment in transcript if segment.get("text"))
    return success("Transcript fetched", f"Fetched transcript for {video_id}.", text=text[:16000], segments=transcript[:200])
