"""Run with python3 -m tools.earth. All input acquisition is explicit."""
import argparse
import json
import sys
from .pack import build_pack, verify_pack
from .prepare import prepare_grip, prepare_opendap, prepare_raster
from .roads import CLASSES


def main(argv=None):
    parser = argparse.ArgumentParser(description="Build/verify offline Solace Earth packs")
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", help="build into a NEW directory")
    build.add_argument("--config", required=True)
    build.add_argument("--output", required=True)
    build.add_argument("--codec", choices=("zstd", "zlib"))
    verify = commands.add_parser("verify", help="verify untrusted pack without modifying it")
    verify.add_argument("pack")
    for name in ("prepare-opendap", "prepare-grip", "prepare-raster"):
        command = commands.add_parser(name)
        command.add_argument("--source", required=True)
        command.add_argument("--output", required=True)
        if name == "prepare-grip":
            command.add_argument("--id-field")
            command.add_argument("--class-field", default="gp_rtp")
            command.add_argument("--unknown-class", choices=CLASSES)
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            manifest = build_pack(args.config, args.output, args.codec)
            result = {"status": "built-and-verified", "output": args.output, **manifest["measurements"]}
        elif args.command == "verify":
            manifest = verify_pack(args.pack)
            result = {"status": "verified", "name": manifest["name"], **manifest["measurements"]}
        elif args.command == "prepare-opendap":
            result = prepare_opendap(args.source, args.output)
        elif args.command == "prepare-raster":
            result = prepare_raster(args.source, args.output)
        else:
            result = prepare_grip(args.source, args.output, args.id_field, args.class_field, args.unknown_class)
    except (ValueError, TypeError, KeyError, OSError) as exc:
        print("earth: " + str(exc), file=sys.stderr)
        return 2
    print(json.dumps(result, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
