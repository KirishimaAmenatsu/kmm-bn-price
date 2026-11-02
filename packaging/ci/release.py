#!/usr/bin/env python3
"""Publish only this run's exact seven native packages, via a draft release."""
import hashlib
import json
import os
from pathlib import Path
import re
import time
import urllib.error
import urllib.parse
import urllib.request

from metadata import validate_assets, validate_tag, version


MAX_TAG_DEREFERENCES = 8
PUBLISH_MAX_ATTEMPTS = 5
PUBLISH_RETRY_SECONDS = 3


def resolve_tag_commit(request, base, tag):
    """Resolve lightweight/annotated tags with bounded, cycle-safe peeling."""
    ref = request(base + "/git/ref/tags/" + urllib.parse.quote(tag, safe=""))["object"]
    seen = set()
    while ref["type"] == "tag":
        sha = ref["sha"]
        if not re.fullmatch(r"[0-9a-f]{40}", sha) or sha in seen or len(seen) >= MAX_TAG_DEREFERENCES:
            raise ValueError("invalid, cyclic, or too deeply nested annotated tag")
        seen.add(sha)
        ref = request(base + "/git/tags/" + sha)["object"]
    if ref["type"] != "commit" or not re.fullmatch(r"[0-9a-f]{40}", ref["sha"]):
        raise ValueError("tag does not resolve to a commit")
    return ref["sha"]


def publish(directory):
    value = version()
    tag = os.environ["GITHUB_REF_NAME"]
    validate_tag(tag, value)
    if os.environ["GITHUB_EVENT_NAME"] != "push" or os.environ["GITHUB_REF"] != "refs/tags/" + tag:
        raise ValueError("only a tag push may publish")
    repo = os.environ["GITHUB_REPOSITORY"]
    sha = os.environ["GITHUB_SHA"]
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo) or not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise ValueError("invalid repository/commit")
    names = validate_assets(directory, value)
    sums = directory / "SHA256SUMS"
    sums.write_text("".join(f"{hashlib.sha256((directory / n).read_bytes()).hexdigest()}  {n}\n" for n in names))
    token = os.environ["GH_TOKEN"]
    base = "https://api.github.com/repos/" + repo

    def request(url, method="GET", body=None, binary=False):
        data = body if binary else (json.dumps(body).encode() if body is not None else None)
        headers = {"Authorization": "Bearer " + token, "Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28"}
        if data is not None:
            headers["Content-Type"] = "application/octet-stream" if binary else "application/json"
        with urllib.request.urlopen(urllib.request.Request(url, data=data, method=method, headers=headers), timeout=120) as response:
            return json.load(response)

    # The tag endpoint may omit drafts. A failed upload must not be silently
    # retried into another draft for the same tag.
    page = 1
    while True:
        releases = request(base + f"/releases?per_page=100&page={page}")
        if any(item["tag_name"] == tag for item in releases):
            raise ValueError("release already exists (including drafts); refusing overwrite")
        if len(releases) < 100:
            break
        page += 1
    try:
        request(base + "/releases/tags/" + tag)
    except urllib.error.HTTPError as error:
        if error.code != 404:
            raise
        error.close()
    else:
        raise ValueError("release already exists; refusing overwrite")
    if resolve_tag_commit(request, base, tag) != sha:
        raise ValueError("tag no longer points to this workflow commit")
    release = request(base + "/releases", "POST", {"tag_name": tag, "target_commitish": sha, "name": tag, "draft": True, "prerelease": False, "body": f"Source commit: {sha}\n\nSeven source-built native packages. RPMs target openSUSE Tumbleweed, DEBs target Debian sid. Unsigned; SHA256SUMS provides integrity, not publisher authentication. Exact KMyMoney runtime version required; rebuild after Qt/KF ABI updates."})
    upload = release["upload_url"].split("{")[0]
    if not upload.startswith("https://uploads.github.com/repos/" + repo + "/releases/"):
        raise ValueError("unexpected upload endpoint")
    for name in names + ["SHA256SUMS"]:
        result = request(upload + "?name=" + urllib.parse.quote(name, safe=""), "POST", (directory / name).read_bytes(), binary=True)
        if result["state"] != "uploaded" or result["size"] != (directory / name).stat().st_size:
            raise ValueError("incomplete upload; leaving release as draft")
    assets = request(base + f"/releases/{release['id']}/assets?per_page=100")
    if {a["name"] for a in assets} != set(names + ["SHA256SUMS"]) or len(assets) != 8:
        raise ValueError("unexpected draft assets; refusing publication")
    # Uploads can take minutes. Drift/deletion here must leave the draft intact.
    # The API cannot atomically check a ref and publish; repository tag rulesets
    # must prohibit updates/deletions to close the remaining race after this GET.
    if resolve_tag_commit(request, base, tag) != sha:
        raise ValueError("tag no longer points to this workflow commit; leaving release as draft")
    for attempt in range(1, PUBLISH_MAX_ATTEMPTS + 1):
        try:
            request(base + f"/releases/{release['id']}", "PATCH", {"draft": False})
            break
        except urllib.error.HTTPError as error:
            if error.code != 422:
                raise
            error.close()
            if attempt == PUBLISH_MAX_ATTEMPTS:
                raise ValueError("publish rejected after retries; leaving release as draft") from error
            status = request(base + f"/releases/{release['id']}")
            if not status["draft"]:
                break
            time.sleep(PUBLISH_RETRY_SECONDS * attempt)


if __name__ == "__main__":
    publish(Path("release-assets"))
