#!/usr/bin/env python3
"""
One-time migration: populate the 8 new parameterized filament behavior fields
onto the vendor fdm_filament_<type> base profiles (and any concrete profiles that
set filament_type directly without inheriting from a type base).

Values are taken verbatim from §5 of parameterize-filament-types-prompt.md.
Run from the repo root:
    python scripts/migrate_builtin_filament_profiles.py

Pass --dry-run to preview without writing.
"""

import argparse
import json
import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).parent.parent
PROFILES_DIR = REPO_ROOT / "resources" / "profiles"

# ---------------------------------------------------------------------------
# §5 tables — byte-for-byte source of truth
# ---------------------------------------------------------------------------
# filament_temp_type: 0=HighTemp, 1=LowTemp, 2=HighLowCompatible, 3=Undefine
TEMP_TYPE = {
    "ABS": 0, "ASA": 0, "PC": 0, "PA": 0, "PA-CF": 0, "PA-GF": 0,
    "PA6-CF": 0, "PET-CF": 0, "PPS": 0, "PPS-CF": 0, "PPA-CF": 0,
    "PPA-GF": 0, "ABS-GF": 0, "ASA-Aero": 0,
    "PLA": 1, "TPU": 1, "PLA-CF": 1, "PLA-AERO": 1, "PVA": 1, "BVOH": 1,
    "HIPS": 2, "PETG": 2, "PE": 2, "PP": 2, "EVA": 2,
    "PE-CF": 2, "PP-CF": 2, "PP-GF": 2, "PHA": 2,
    # default 3 (Undefine) for everything else
}

COOLING_SMART_ZONE = {"PLA", "PETG", "ABS"}  # true; else false

BED_ADHESION = {
    "PLA": 0.02, "PET": 0.3, "PETG": 0.3, "ABS": 0.1, "ASA": 0.1,
    # default 0.02
}

THERMAL_LENGTH = {
    "ABS": 100.0, "PA-CF": 100.0, "PET-CF": 100.0, "PC": 40.0, "TPU": 1000.0,
    # default 200.0
}

BRIM_ADHESION_COEFF = {
    "PETG": 2.0, "PCTG": 2.0, "TPU": 0.5,
    # default 1.0
}

SMALL_ISLAND_THRESHOLD = {
    "PETG": 20.0,
    # default 10.0
}

CHAMBER_TEMP_LIMIT = {
    "PLA": 45, "PLA-CF": 45, "PVA": 45, "TPU": 50,
    "PETG": 55, "PCTG": 55, "PETG-CF": 55,
    # default 0
}

IS_FLEXIBLE = {"TPU"}  # true; else false

# Defaults (used to decide whether to skip writing a key)
DEFAULTS = {
    "filament_temp_type": 3,
    "filament_cooling_smart_zone": 0,
    "filament_bed_adhesion_strength": 0.02,
    "filament_thermal_length": 200.0,
    "filament_brim_adhesion_coeff": 1.0,
    "filament_small_island_threshold": 10.0,
    "filament_chamber_temp_limit": 0,
    "filament_is_flexible": 0,
}


def values_for_type(ft: str) -> dict:
    """Return {field: value} for the given filament type string, omitting defaults."""
    result = {}
    v = TEMP_TYPE.get(ft, 3)
    if v != DEFAULTS["filament_temp_type"]:
        result["filament_temp_type"] = [str(v)]

    v = 1 if ft in COOLING_SMART_ZONE else 0
    if v != DEFAULTS["filament_cooling_smart_zone"]:
        result["filament_cooling_smart_zone"] = [str(v)]

    v = BED_ADHESION.get(ft, 0.02)
    if v != DEFAULTS["filament_bed_adhesion_strength"]:
        result["filament_bed_adhesion_strength"] = [str(v)]

    v = THERMAL_LENGTH.get(ft, 200.0)
    if v != DEFAULTS["filament_thermal_length"]:
        result["filament_thermal_length"] = [str(v)]

    v = BRIM_ADHESION_COEFF.get(ft, 1.0)
    if v != DEFAULTS["filament_brim_adhesion_coeff"]:
        result["filament_brim_adhesion_coeff"] = [str(v)]

    v = SMALL_ISLAND_THRESHOLD.get(ft, 10.0)
    if v != DEFAULTS["filament_small_island_threshold"]:
        result["filament_small_island_threshold"] = [str(v)]

    v = CHAMBER_TEMP_LIMIT.get(ft, 0)
    if v != DEFAULTS["filament_chamber_temp_limit"]:
        result["filament_chamber_temp_limit"] = [str(v)]

    v = 1 if ft in IS_FLEXIBLE else 0
    if v != DEFAULTS["filament_is_flexible"]:
        result["filament_is_flexible"] = [str(v)]

    return result


NEW_FIELDS = set(DEFAULTS.keys())


def migrate_file(path: pathlib.Path, dry_run: bool) -> bool:
    """Migrate a single JSON file. Returns True if changed."""
    try:
        text = path.read_text(encoding="utf-8")
        data = json.loads(text)
    except Exception as e:
        print(f"  SKIP {path.name}: {e}", file=sys.stderr)
        return False

    # Only act on profiles that directly declare filament_type
    ft_raw = data.get("filament_type")
    if not ft_raw:
        return False
    ft = ft_raw[0] if isinstance(ft_raw, list) else str(ft_raw)

    new_fields = values_for_type(ft)
    changed = False
    for k, v in new_fields.items():
        if k not in data:
            data[k] = v
            changed = True

    if not changed:
        return False

    print(f"  {'DRY ' if dry_run else ''}WRITE {path.relative_to(REPO_ROOT)}")
    for k, v in new_fields.items():
        if k not in (json.loads(text) or {}):
            print(f"    + {k} = {v[0]}")

    if not dry_run:
        path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="Print changes without writing")
    args = parser.parse_args()

    total = modified = 0
    for vendor_dir in sorted(PROFILES_DIR.iterdir()):
        filament_dir = vendor_dir / "filament"
        if not filament_dir.is_dir():
            continue
        for jf in sorted(filament_dir.glob("*.json")):
            total += 1
            if migrate_file(jf, args.dry_run):
                modified += 1

    print(f"\n{'[DRY RUN] ' if args.dry_run else ''}Done: {modified}/{total} files updated.")


if __name__ == "__main__":
    main()
