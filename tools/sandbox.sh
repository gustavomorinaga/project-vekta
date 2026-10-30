#!/usr/bin/env bash
# Runs a command in a bubblewrap sandbox: the whole system, the repository and your game files read-only, only the
# work folder ($KZ_WORK, default work/) and a private /tmp writable, and no network. Use it for anything that reads
# the game (lift, setup-vfs, build, run), so nothing can write into your game files or reach the network.
#   tools/sandbox.sh [--gpu] [--net] [--] COMMAND [ARG...]
#     --gpu   the host's /dev (GPU, input devices) and the display and sound sockets, for runs with a window
#     --net   keep the network (nothing in the port needs it; for fetching the ps3recomp submodule, say)
# Linux only; needs bwrap (bubblewrap). Caches (XDG_CACHE_HOME) and Python's bytecode go under $KZ_WORK.
# (After Project Helghast's tools/sandbox.sh, MIT: the same rule, game and repository read-only, only work/ writable.)
set -euo pipefail
KZ_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$KZ_ROOT/tools/env.sh"
GPU=0; NET=0
while (( $# )); do
  case "$1" in
    --gpu) GPU=1 ;;
    --net) NET=1 ;;
    --) shift; break ;;
    -*) echo "sandbox: unknown option $1" >&2; exit 2 ;;
    *) break ;;
  esac
  shift
done
(( $# )) || { echo "usage: tools/sandbox.sh [--gpu] [--net] [--] COMMAND [ARG...]" >&2; exit 2; }
command -v bwrap >/dev/null || { echo "sandbox: bwrap not found (install bubblewrap)" >&2; exit 2; }
mkdir -p "$KZ_WORK" "$KZ_WORK/cache"

args=(--ro-bind / / --proc /proc --tmpfs /tmp --bind "$KZ_WORK" "$KZ_WORK"
      --unshare-pid --unshare-ipc --unshare-uts --die-with-parent --new-session
      --setenv XDG_CACHE_HOME "$KZ_WORK/cache" --setenv PYTHONPYCACHEPREFIX "$KZ_WORK/cache/pycache"
      --chdir "$PWD")
if (( GPU )); then
  args+=(--dev-bind /dev /dev)
  # display and sound: the X11 sockets live under /tmp, which the sandbox replaces; connecting through a read-only
  # mount works, so the rest (Wayland, PipeWire, PulseAudio under $XDG_RUNTIME_DIR) needs nothing more
  [[ -d /tmp/.X11-unix ]] && args+=(--ro-bind /tmp/.X11-unix /tmp/.X11-unix)
else
  args+=(--dev /dev)
fi
(( NET )) || args+=(--unshare-net)
exec bwrap "${args[@]}" -- "$@"
