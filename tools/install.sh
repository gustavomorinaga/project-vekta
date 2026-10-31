#!/bin/sh
# One command from a source release or a checkout to a built port: asks where your files are, checks them, then
# patches, lifts and builds with one progress bar (tools/release/firstrun.py --interactive --bar; its options pass
# through, e.g. --jobs 4). Run it again after an update, or to point at other files: finished steps are skipped.
KZ_ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
exec python3 "$KZ_ROOT/tools/release/firstrun.py" --interactive "$@"
