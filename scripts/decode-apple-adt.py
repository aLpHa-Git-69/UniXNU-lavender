#!/usr/bin/env python3
"""Decode and validate a supplied Apple DeviceTree IM4P as a local ADT input."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "UniXNU/scripts"))
sys.path.insert(0, str(ROOT / "ios-lavender/tools"))
from unixnu.ipsw import im4p_payload
import adt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = args.input.read_bytes()
    packed = im4p_payload(source)
    if packed[:4] in (b"bvx2", b"bvx1", b"bvx-", b"bvxn"):
        try:
            import lzfse
        except ImportError as error:
            raise RuntimeError("install the lzfse Python codec for this local decoding step") from error
        decoded = lzfse.decompress(packed)
    else:
        decoded = packed
    tree, used = adt.parse(decoded)
    if any(decoded[used:]):
        raise ValueError("nonzero data follows the Apple device tree")
    products = [node for node in tree.children if node.name == "product"]
    if len(products) != 1:
        raise ValueError("Apple input must have exactly one /product node")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(decoded)
    report = {"input": str(args.input), "input_bytes": len(source),
              "input_sha256": hashlib.sha256(source).hexdigest(),
              "output_bytes": len(decoded), "parsed_bytes": used,
              "output_sha256": hashlib.sha256(decoded).hexdigest(),
              "product_properties": [key for key, flag, value in products[0].props if not flag],
              "signature_verified": False}
    args.output.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Apple ADT decoded and parsed: {args.output} ({len(decoded)} bytes)")


if __name__ == "__main__":
    main()
