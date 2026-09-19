"""Archive reviewed DoughWorld sources and evidence; dry-run unless --apply.

Run with ordinary desktop Python after UnrealEditor has been closed. This file
never starts UE, imports assets, rebuilds a level, edits native source, or deletes
files. A copy records bytes and provenance, not an acceptance-test verdict.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime, timezone

HERE = Path(__file__).resolve().parent
PROJECT = Path("E:/Unreal Project/GDATtest").resolve()
ARCHIVE = (PROJECT / "Content/Maps/DoughWorldDepth").resolve()
NATIVE = (PROJECT / "Source/GDATtest").resolve()
DRAFT = HERE / "Planning/DeliveryDraft/SourceArchiveDraft.json"

DOCUMENTS = (
    "SceneReport.md", "TestResults.md", "Integration.md", "EnvironmentAssetList.md",
    "SourceDiscrepancies.md", "AssetRecommendations.md", "ArchiveInstructions.md", "完成说明.md",
)
# These are actual verification outputs, not broad diagnostic-directory scans.
EVIDENCE = (
    "EditorVerification.json", "HoleGameCollision_PIE.json", "FullRuntime.json",
    "WalkRuntime.json", "CameraFadeRuntime.json", "PortalRuntime.json",
    "SceneFadeComparison.json", "DeepPolishRuntime.json", "ActionsLifecycleRuntime.json",
    "ResumeEnvironmentAudit.json", "EnvironmentAttachmentRepair.json", "FinalPersistence.json", "RollCollisionRuntime.json",
    "SpatialPolish.json", "BridgeContactFit.json", "BridgeRuntime.json", "FinalValidationSequence.json", "SourceImportExclusions.json", "EditorOverview.json",
)
AUTHORING_SCRIPTS = (
    "scene_common.py", "build_depth_scene.py", "build_prototype.py", "build_all.py",
    "import_geometry.py", "finish_liquid_material.py", "final_polish.py",
    "repair_environment_attachments.py", "repair_masks.py", "prepare_candidate.py",
    "finish_spatial_polish.py", "fit_bridge_contact.py", "mcp_local.py", "ue_call.py", "exclude_archived_sources.py", "capture_editor_overview.py",
)
VALIDATION_SCRIPTS = (
    "verify_editor.py", "verify_holes_game_collision.py", "full_runtime.py",
    "walk_runtime.py", "run_fade_validation.py", "portal_runtime.py",
    "fade_scene_comparison.py", "recapture_deep.py", "actions_lifecycle_runtime.py",
    "resume_environment_audit.py", "CameraCode/validate_camera_fade_pie.py", "roll_collision_runtime.py",
    "recapture_bridge.py", "final_persistence.py", "run_final_validation.py",
)
DATA = (
    "PlacementManifest.json", "BoundaryManifest.json", "DepthOverrides.json",
    "CameraBaseline.json", "Planning/ModulePlan.json", "Planning/IntegrityCheck.json",
    "Planning/DeliveryDraft/ModuleCoverageDraft.json",
    "Planning/DeliveryDraft/ManifestEnrichmentAudit.json",
    "TerrainDesign/actual_low_foliage_report.json",
)


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def inside(path: Path, root: Path, *, allow_root: bool = False) -> Path:
    resolved = path.resolve()
    if not resolved.is_relative_to(root) or (resolved == root and not allow_root):
        raise ValueError(f"Path escapes permitted directory: {path} -> {resolved}")
    return resolved


def utc_iso(seconds: float) -> str:
    return datetime.fromtimestamp(seconds, timezone.utc).isoformat()


def timestamp(text: str | None) -> float | None:
    if not text:
        return None
    dt = datetime.fromisoformat(text.replace("Z", "+00:00"))
    if dt.tzinfo is None:
        raise ValueError("--verify-after requires a UTC offset or Z")
    return dt.timestamp()


def unreal_processes() -> list[dict[str, str]]:
    # Read-only process inventory; never terminate processes from this script.
    command = ["tasklist.exe", "/FI", "IMAGENAME eq UnrealEditor.exe", "/FO", "CSV", "/NH"]
    result = subprocess.run(command, check=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, errors="replace")
    return [{"image": row[0], "pid": row[1]} for row in csv.reader(io.StringIO(result.stdout))
            if len(row) >= 2 and row[0].casefold() == "unrealeditor.exe"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="Copy after all preflight checks pass")
    parser.add_argument("--verify-after", help="Only copy runtime JSON/captures modified after this ISO timestamp")
    parser.add_argument("--evidence-list", type=Path,
                        help="JSON array of additional workspace-relative final evidence paths")
    parser.add_argument("--build-log", type=Path, help="Optional final native build log to include verbatim")
    args = parser.parse_args()
    cutoff = timestamp(args.verify_after)
    now = datetime.now(timezone.utc)
    report = {"schema": 1, "created_utc": now.isoformat(), "apply_requested": args.apply,
              "archive_root": str(ARCHIVE), "verify_after": args.verify_after,
              "copy_success_is_not_test_pass": True,
              "native_reference_policy": "Read project Source/GDATtest; never replace it with workspace copies.",
              "native_compile_status": "Consult the final build log and TestResults; hashes alone do not prove compilation.",
              "files": [], "skipped": [], "native_workspace_comparison": [], "errors": []}
    planned: dict[Path, dict] = {}

    def add(source: Path, relative_target: Path | str, kind: str, *, optional=False, fresh=False,
            previous_hash: str | None = None):
        source = source.resolve()
        if not (source.is_relative_to(HERE) or source.is_relative_to(NATIVE)):
            raise ValueError(f"Source is outside the two reviewed source roots: {source}")
        target = inside(ARCHIVE / relative_target, ARCHIVE)
        if not (target.is_relative_to(ARCHIVE / "Source") or
                target.is_relative_to(ARCHIVE / "Documentation")):
            raise ValueError(f"Target must be in Source or Documentation: {target}")
        if not source.is_file():
            if optional:
                report["skipped"].append({"source": str(source), "reason": "optional_file_not_present"})
                return
            raise FileNotFoundError(source)
        stat = source.stat()
        if fresh and cutoff is not None and stat.st_mtime < cutoff:
            report["skipped"].append({"source": str(source), "reason": "before_verify_after",
                                      "modified_utc": utc_iso(stat.st_mtime)})
            return
        source_hash = digest(source)
        entry = {"source": str(source), "target": str(target), "kind": kind,
                 "source_sha256": source_hash, "bytes": stat.st_size,
                 "modified_utc": utc_iso(stat.st_mtime)}
        if previous_hash:
            entry["draft_sha256"] = previous_hash
            entry["changed_since_draft"] = source_hash != previous_hash
        if target in planned:
            if planned[target]["source_sha256"] != source_hash:
                raise ValueError(f"Conflicting sources for {target}")
            return
        entry["target_sha256_before"] = digest(target) if target.is_file() else None
        entry["action"] = "skip_identical" if entry["target_sha256_before"] == source_hash else "copy"
        planned[target] = entry

    try:
        draft = json.loads(DRAFT.read_text(encoding="utf-8-sig"))
        for entry in draft["files"]:
            src = Path(entry.get("native_source") or entry["workspace_source"])
            dst = inside(Path(entry["project_target"]), ARCHIVE).relative_to(ARCHIVE)
            kind = "native_reference" if entry.get("native_source") else "source_from_reviewed_draft"
            add(src, dst, kind, previous_hash=entry.get("source_sha256"))

        # Include the final character/controller integration, not just the five new classes.
        for src in sorted(NATIVE.iterdir()):
            if src.is_file() and (src.suffix in {".h", ".cpp"} or src.name.endswith(".Build.cs")):
                add(src, Path("Source/NativeCodeReference") / src.name, "native_reference")
                workspace_copy = HERE / "CameraCode" / src.name
                if workspace_copy.is_file():
                    comparison = {"native": str(src), "workspace": str(workspace_copy),
                                  "native_sha256": digest(src), "workspace_sha256": digest(workspace_copy)}
                    comparison["identical"] = comparison["native_sha256"] == comparison["workspace_sha256"]
                    report["native_workspace_comparison"].append(comparison)
                    if not comparison["identical"]:
                        report["errors"].append("Native/workspace source mismatch; reconcile and compile before archive: " + src.name)

        # Preserve authoring relative paths in a separate complete reference subtree.
        authoring = Path("Source/AuthoringProject")
        for folder in ("Handoff", "TerrainDesign", "Planning/Geometry"):
            for src in sorted((HERE / folder).rglob("*")):
                if src.is_file() and "__pycache__" not in src.parts and src.suffix in {
                    ".json", ".md", ".py", ".png", ".npy", ".r16", ".obj", ".mtl", ".h", ".cpp"
                }:
                    add(src, authoring / src.relative_to(HERE), "authoring_reference")
        for name in AUTHORING_SCRIPTS + VALIDATION_SCRIPTS:
            add(HERE / name, authoring / name, "authoring_script_reference", optional=True)
        for name in DATA:
            add(HERE / name, authoring / name, "placement_or_design_data", optional=True)
        add(HERE / "archive_delivery.py", "Source/archive_delivery.py", "archive_script_reference")

        for name in DOCUMENTS:
            add(HERE / name, Path("Documentation") / name, "reviewed_document",
                optional=name == "AssetRecommendations.md")
        for src in sorted((HERE / "CameraCode").glob("*.md")):
            note_folder = "NativeNotes" if src.name in {"翻滚距离修复说明.md", "角色生命周期测试修订说明.md"} else "NativeIntegration"
            add(src, Path("Documentation") / note_folder / src.name, "native_integration_document")
        for name in DATA:
            add(HERE / name, Path("Documentation/Data") / name, "placement_or_design_data", optional=True)
        for name in EVIDENCE:
            add(HERE / name, Path("Documentation/Evidence") / name, "runtime_evidence_verbatim",
                optional=True, fresh=True)
        for src in sorted((HERE / "Captures").glob("[0-9][0-9]_*.png")):
            add(src, Path("Documentation/Captures") / src.name, "runtime_capture", fresh=True)

        if args.evidence_list:
            list_path = inside(args.evidence_list, HERE)
            extra = json.loads(list_path.read_text(encoding="utf-8-sig"))
            if not isinstance(extra, list) or not all(isinstance(v, str) for v in extra):
                raise ValueError("--evidence-list must be a JSON array of relative path strings")
            for name in extra:
                relative = Path(name)
                if relative.is_absolute() or ".." in relative.parts:
                    raise ValueError("Additional evidence path must be workspace-relative: " + name)
                add(HERE / relative, Path("Documentation/Evidence/Additional") / relative,
                    "operator_selected_final_evidence_verbatim", fresh=True)
        if args.build_log:
            log = inside(args.build_log, HERE)
            add(log, Path("Documentation/Build") / log.name, "build_log_verbatim")
        if args.apply:
            running = unreal_processes()
            if running:
                report["errors"].append("UnrealEditor is still running; close it normally before applying: " + str(running))

        report["files"] = list(planned.values())
        if report["errors"]:
            report["status"] = "blocked_preflight_no_project_copies"
        elif not args.apply:
            report["status"] = "dry_run_ready"
        else:
            for target, entry in planned.items():
                source = Path(entry["source"])
                # Revalidate immediately before each write, including newly created parents.
                inside(target, ARCHIVE)
                if digest(source) != entry["source_sha256"]:
                    raise RuntimeError("Source changed during archive; rerun from a stable snapshot: " + str(source))
                target.parent.mkdir(parents=True, exist_ok=True)
                inside(target, ARCHIVE)
                if entry["action"] == "copy":
                    shutil.copy2(source, target)
                entry["target_sha256_after"] = digest(target)
                if entry["target_sha256_after"] != entry["source_sha256"]:
                    raise RuntimeError("Post-copy hash mismatch: " + str(target))
            report["status"] = "copied_and_hash_verified"
    except Exception as exc:
        report["status"] = "failed"
        report["errors"].append(f"{type(exc).__name__}: {exc}")
        report["files"] = list(planned.values())

    report["counts"] = {"planned_files": len(planned),
                        "copy": sum(e["action"] == "copy" for e in planned.values()),
                        "identical": sum(e["action"] == "skip_identical" for e in planned.values()),
                        "skipped": len(report["skipped"]), "errors": len(report["errors"])}
    local_report = HERE / ("ArchiveReport.json" if args.apply else "ArchiveDryRun.json")
    local_report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    if report["status"] == "copied_and_hash_verified":
        target_report = inside(ARCHIVE / "Documentation/ArchiveReport.json", ARCHIVE)
        shutil.copy2(local_report, target_report)
        if digest(local_report) != digest(target_report):
            raise RuntimeError("Archive report hash mismatch")
    print(json.dumps({"status": report["status"], "counts": report["counts"],
                      "report": str(local_report), "errors": report["errors"]}, ensure_ascii=False))
    return 0 if report["status"] in {"dry_run_ready", "copied_and_hash_verified"} else 2


if __name__ == "__main__":
    sys.exit(main())
