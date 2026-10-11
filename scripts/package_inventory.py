"""Build a public-catalog inventory without equating package format with runtime."""

import argparse
from contextlib import closing
from datetime import datetime
import json
from pathlib import Path
import re
import sqlite3


def project(rows):
    if not isinstance(rows, list):
        raise ValueError("inventory must be an array of product records")
    seen = set()
    result = []
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("product record must be an object")
        product_id = row.get("id")
        if not isinstance(product_id, str) or not re.fullmatch(r"[A-Z0-9]{12}", product_id):
            raise ValueError("product ID must contain 12 uppercase letters/digits")
        if product_id in seen:
            raise ValueError(f"duplicate product ID: {product_id}")
        seen.add(product_id)
        for field in ("title", "fmt", "arch", "fw", "attrs"):
            if not isinstance(row.get(field), str):
                raise ValueError(f"{product_id}: {field} must be a string")
        for field in ("fullTrust", "customInstall"):
            if type(row.get(field)) is not bool:
                raise ValueError(f"{product_id}: {field} must be a boolean")
        drivers = row.get("drivers")
        if type(drivers) is not int or drivers < 0:
            raise ValueError(f"{product_id}: drivers must be a nonnegative integer")
        frameworks = [item for item in row["fw"].split(";") if item]
        if row["fmt"] == "(none)":
            signal = "no-desktop-package-in-snapshot"
        elif row["fullTrust"]:
            signal = "declares-full-trust"
        elif any(name.startswith("Microsoft.NET.Native.") for name in frameworks):
            signal = "declares-dotnet-native"
        else:
            signal = "runtime-unknown"
        # This projection deliberately excludes keys, URLs and other raw catalog fields.
        result.append((
            product_id, row["title"], row["fmt"], row["arch"],
            int(row["fullTrust"]), int(row["customInstall"]),
            json.dumps(frameworks), drivers, row["attrs"], signal,
        ))
    return result


def project_evidence(rows, product_ids):
    result = []
    if not isinstance(rows, list):
        raise ValueError("evidence must be an array")
    for row in rows:
        if not isinstance(row, dict) or row.get("product_id") not in product_ids:
            raise ValueError("evidence must refer to an inventoried product")
        if row.get("kind") not in ("manifest-observed", "catalog-declaration"):
            raise ValueError("evidence kind must distinguish manifest from catalog")
        if row.get("runtime_type") not in ("uwp", "contains-desktop-process", "unknown"):
            raise ValueError("unsupported runtime classification")
        if not all(isinstance(row.get(key), str) and row[key]
                   for key in ("source", "detail", "observed_at")):
            raise ValueError("evidence needs source, detail and observation time")
        datetime.fromisoformat(row["observed_at"])
        result.append(tuple(row[key] for key in (
            "product_id", "kind", "runtime_type", "source", "detail", "observed_at",
        )))
    return result


def build_database(snapshot, output, market, observed_at, evidence=()):
    datetime.fromisoformat(observed_at)
    products = project(snapshot)
    observations = project_evidence(list(evidence), {row[0] for row in products})
    # Refuse replacement so a failed/partial refresh cannot destroy prior observations.
    with output.open("xb"):
        pass
    try:
        with closing(sqlite3.connect(output)) as db, db:
            db.execute("PRAGMA foreign_keys = ON")
            db.executescript("""
                CREATE TABLE metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL);
                CREATE TABLE products (
                    product_id TEXT PRIMARY KEY, title TEXT NOT NULL,
                    package_format TEXT NOT NULL, architectures TEXT NOT NULL,
                    declares_full_trust INTEGER NOT NULL,
                    declares_custom_install INTEGER NOT NULL,
                    frameworks_json TEXT NOT NULL, driver_dependency_count INTEGER NOT NULL,
                    attributes TEXT NOT NULL, runtime_signal TEXT NOT NULL
                );
                CREATE TABLE runtime_evidence (
                    product_id TEXT NOT NULL REFERENCES products(product_id),
                    kind TEXT NOT NULL, runtime_type TEXT NOT NULL, source TEXT NOT NULL,
                    detail TEXT NOT NULL, observed_at TEXT NOT NULL
                );
                CREATE VIEW encrypted_desktop_candidates AS
                    SELECT * FROM products
                    WHERE lower(package_format) IN ('eappx', 'eappxbundle')
                      AND declares_full_trust = 1;
            """)
            db.executemany("INSERT INTO metadata VALUES (?, ?)", [
                ("schema_version", "1"), ("market", market), ("observed_at", observed_at),
                ("source", "Microsoft public PC Game Pass / Display Catalog"),
                ("scope", "Selected Desktop package projection per product; not all SKU variants"),
                ("classification_rule", "Format and capabilities are not gameplay verdicts"),
            ])
            db.executemany("INSERT INTO products VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", products)
            db.executemany("INSERT INTO runtime_evidence VALUES (?, ?, ?, ?, ?, ?)", observations)
            if db.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
                raise ValueError("SQLite inventory integrity check failed")
    except Exception:
        output.unlink()
        raise
    return len(products)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--market", required=True)
    parser.add_argument("--observed-at", required=True)
    parser.add_argument("--snapshot-encoding", default="utf-8")
    parser.add_argument("--evidence", type=Path)
    args = parser.parse_args()
    evidence = json.loads(args.evidence.read_text(encoding="utf-8")) if args.evidence else []
    count = build_database(
        json.loads(args.snapshot.read_text(encoding=args.snapshot_encoding)),
        args.output, args.market, args.observed_at, evidence,
    )
    print(f"Created {args.output}: {count} public product records; gameplay unverified.")


if __name__ == "__main__":
    main()
