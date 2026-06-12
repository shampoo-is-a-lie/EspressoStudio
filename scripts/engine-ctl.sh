#!/usr/bin/env bash
# Start/stop/status/log for EspressoEngine inside the espresso-dev toolbox.
# The engine must be daemonized *inside* the container (setsid + nohup) or the
# podman exec session teardown SIGKILLs it.
set -e
cd "$(dirname "$0")/.."

case "${1:-start}" in
  start)
    # Daemonize inside the container (setsid+nohup, session exits at once) so
    # the podman exec teardown can't reap the engine. Health check happens in
    # a second, separate exec session.
    toolbox run -c espresso-dev bash -c '
      pkill -x EspressoEngine 2>/dev/null
      setsid nohup ./engine/build/EspressoEngine > /tmp/espresso-engine.log 2>&1 < /dev/null &'
    sleep 4
    if toolbox run -c espresso-dev bash -c 'pgrep -x EspressoEngine > /dev/null'; then
      echo "engine started"
    else
      echo "engine failed to start:" >&2
      toolbox run -c espresso-dev tail -5 /tmp/espresso-engine.log >&2
      exit 1
    fi
    ;;
  stop)
    toolbox run -c espresso-dev bash -c 'pkill -x EspressoEngine 2>/dev/null || true'
    ;;
  status)
    toolbox run -c espresso-dev bash -c 'pgrep -a EspressoEngine || echo "not running"'
    ;;
  log)
    toolbox run -c espresso-dev tail -n "${2:-30}" /tmp/espresso-engine.log
    ;;
  *)
    echo "usage: $0 {start|stop|status|log [n]}" >&2
    exit 1
    ;;
esac
