#!/usr/bin/env python3
"""Independent NumPy references from hash-verified original orbital ASCII.

Requires NumPy for fixture regeneration only. Does not use Aster's parser,
interpolation or scientific library. Native knots use exact parsed doubles.
"""
from pathlib import Path
import hashlib
import io
import json
import zlib
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "resources/astronomy/solutions"
catalogue = json.loads((DATA / "catalogue.json").read_text(encoding="utf-8"))["solutions"]
fixtures = []
for key, entry in catalogue.items():
    raw = zlib.decompress((DATA / entry["resource"]).read_bytes()[4:])
    assert hashlib.sha256(raw).hexdigest() == entry["source_sha256"]
    text = raw.decode("utf-8-sig")
    if entry["format"] == "zb_ecc":
        lines = [line for line in text.splitlines() if line and not line.startswith("#")]
        values = np.loadtxt(lines[1:])
        ages = values[:, 0]
        channels = {"eccentricity": values[:, 1]}
    else:
        values = np.loadtxt(io.StringIO(text))
        ages = -values[:, 0]
        channels = {"eccentricity": values[:, 1]} if entry["format"] == "la2010_ecc" else {
            "obliquity": values[:, 1], "axial_precession": values[:, 2], "precession": values[:, 3]
        }
    assert np.all(np.isfinite(values)) and np.all(np.diff(ages) > 0)
    assert len(ages) == entry["rows"] and ages[0] == 0 and ages[-1] == entry["maximum_age_ka"]
    rng = np.random.default_rng(20261102)
    indexes = sorted(set([0, 1, len(ages)//2, len(ages)-2, len(ages)-1] + list(rng.integers(0, len(ages), size=16))))
    knots = [{"age": float(ages[i]), "values": {k: float(v[i]) for k, v in channels.items()}} for i in indexes]
    midpoints = []
    for i in indexes[:-1]:
        age = float(ages[i] + .37*(ages[i+1]-ages[i]))
        result = {k: float(np.interp(age, ages, v)) for k, v in channels.items() if k != "axial_precession"}
        if "axial_precession" in channels:
            angle = float(np.interp(age, ages, np.unwrap(channels["axial_precession"])))
            result["axial_precession"] = float(np.arctan2(np.sin(angle), np.cos(angle)))
        midpoints.append({"age": age, "values": result})
    fixtures.append({"id": key, "source_sha256": entry["source_sha256"], "rows": len(ages),
                     "maximum_age": float(ages[-1]), "knots": knots, "interpolated": midpoints})
output = {"origin": "Official original ASCII files, independent Python NumPy parser; numpy.interp and unwrap for angle reference. Seed 20261102. Native knots are exact parsed doubles.", "solutions": fixtures}
Path(__file__).with_name("orbital_tables_reference.json").write_text(json.dumps(output, indent=2)+"\n", encoding="utf-8")
print("Generated", len(fixtures), "orbital solution reference sets")
