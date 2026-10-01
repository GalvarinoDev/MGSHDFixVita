#!/bin/bash
# Control the Vita through the vitacompanion plugin: FTP on port 1337, commands on port 1338.
#   tools/vita/vita.sh <command> [args]
#
# Read-only commands:
#   status                    check that the Vita answers, show if the Vita is free
#   ls <remote-dir>           list a folder, e.g. ux0:/rePatch
#   pull <remote> <local>     download a file or a folder
#   backup <TITLEID>          download ux0:/rePatch/<TITLEID> to local/backups/<date>_<TITLEID>
# Commands that change the Vita (they stop if another test run uses the Vita):
#   wake                      screen on, no sleep, swipe the lock screen open
#   sleep                     screen off (this also locks the Vita)
#   launch <TITLEID>          start a game
#   quit                      close all apps ("quit <TITLEID>" does not work)
#   push <local> <remote>     upload a file or a folder
#   rm <remote>               delete a file or a folder (only under ux0:/rePatch or ux0:/data/mgshdfixvita)
#   restore <backup-dir>      put a backup from "backup" back on the Vita
#   cmd "<command>"           send any vitacompanion command
#
# Settings come from local/vita.env (git ignores local/):
#   VITA_IP=<ip>
#   TS_LOCK=<path>            lock file of another project that shares the Vita (optional)
# Remote paths use the form ux0:/path.

cd "$(dirname "$0")/../.." || exit 1
[ -f local/vita.env ] && . local/vita.env
[ -n "$VITA_IP" ] || { echo "vita: no Vita IP. Write VITA_IP=<ip> in local/vita.env"; exit 1; }
export VITA_IP
FTP="ftp://$VITA_IP:1337"

vc() { python3 tools/vita/vcmd.py "$1"; }
u() { printf '%s/%s' "$FTP" "$(printf '%s' "${1#/}" | sed 's/ /%20/g')"; }

# Hold our own lock and a shared lock on the other project's lock file until this script ends.
# The other file is opened read-only. While we hold it, its test runs cannot start, and the reverse.
# VITA_LOCKED=1 means that the caller (a test script) already holds both locks.
guard() {
    [ "$VITA_LOCKED" = 1 ] && return
    mkdir -p local
    exec 8>local/vita.lock
    flock -n 8 || { echo "vita: another MGSHDFixVita command uses the Vita"; exit 4; }
    if [ -n "$TS_LOCK" ] && [ -e "$TS_LOCK" ]; then
        exec 7<"$TS_LOCK"
        flock -n -s 7 || { echo "vita: a Twin Snakes test run uses the Vita. Try again later."; exit 4; }
    fi
}

ts_state() {
    [ -n "$TS_LOCK" ] && [ -e "$TS_LOCK" ] || { echo "not set"; return; }
    if flock -n -s "$TS_LOCK" true; then echo "free"; else echo "BUSY (Twin Snakes test run)"; fi
}

# LIST lines: perm links owner group size month day time name
entries() {
    curl -s -f --max-time 30 "$(u "$1")/" | tr -d '\r' | while read -r perm _ _ _ _ _ _ _ name; do
        [ -z "$name" ] || [ "$name" = . ] || [ "$name" = .. ] && continue
        echo "${perm:0:1} $name"
    done
}

# Prints d (folder), f (file) or nothing (missing).
kind() {
    local p=${1%/}
    entries "${p%/*}" | while read -r t name; do [ "$name" = "${p##*/}" ] && { [ "$t" = d ] && echo d || echo f; break; }; done
}

pull_any() {
    if [ "$(kind "$1")" = d ]; then
        mkdir -p "$2"
        entries "$1" | while read -r t name; do
            if [ "$t" = d ]; then pull_any "$1/$name" "$2/$name" || return 1
            else curl -s -f --max-time 300 "$(u "$1/$name")" -o "$2/$name" || { echo "vita: download failed: $1/$name"; return 1; }; fi
        done
    else
        mkdir -p "$(dirname "$2")"
        curl -s -f --max-time 300 "$(u "$1")" -o "$2" || { echo "vita: download failed: $1"; return 1; }
    fi
}

push_any() {
    if [ -d "$1" ]; then
        (cd "$1" && find . -type f ! -name .no_repatch | sed 's#^\./##') | while read -r f; do
            curl -s -f --max-time 300 --ftp-create-dirs -T "$1/$f" "$(u "$2/$f")" || { echo "vita: upload failed: $2/$f"; return 1; }
        done
    else
        curl -s -f --max-time 300 --ftp-create-dirs -T "$1" "$(u "$2")" || { echo "vita: upload failed: $2"; return 1; }
    fi
}

rm_any() {
    case "$1" in
        ux0:/rePatch/?*|ux0:/data/mgshdfixvita|ux0:/data/mgshdfixvita/*) ;;
        *) echo "vita: rm is only allowed under ux0:/rePatch/ and ux0:/data/mgshdfixvita"; return 1;;
    esac
    case "$1" in *..*) echo "vita: rm does not accept .. in paths"; return 1;; esac
    local k; k=$(kind "$1")
    [ -n "$k" ] || return 0
    if [ "$k" = d ]; then
        entries "$1" | while read -r t name; do
            if [ "$t" = d ]; then rm_any "$1/$name" || return 1
            else curl -s -f --max-time 20 -Q "DELE /$1/$name" "$FTP/" -o /dev/null || { echo "vita: delete failed: $1/$name"; return 1; }; fi
        done
        curl -s -f --max-time 20 -Q "RMD /$1" "$FTP/" -o /dev/null || { echo "vita: delete failed: $1"; return 1; }
    else
        curl -s -f --max-time 20 -Q "DELE /$1" "$FTP/" -o /dev/null || { echo "vita: delete failed: $1"; return 1; }
    fi
}

# Swipe from the top right to the bottom left. Touch coordinates are 1920x1088.
unlock() {
    for i in 0 1 2 3 4 5 6 7 8; do vc "press front-touch 0 $((1800 - i * 200)) $((100 + i * 110))" >/dev/null; sleep 0.03; done
    vc "release all" >/dev/null
}

c=$1; shift
case "$c" in
    status)
        v=$(vc version) || exit 2
        echo "$v" | grep -q vitacompanion || { echo "vita: no vitacompanion reply from $VITA_IP"; exit 2; }
        echo "vita: $v at $VITA_IP"
        echo "vita: Twin Snakes lock: $(ts_state)";;
    ls)
        [ -n "$1" ] || { echo "usage: vita.sh ls <remote-dir>"; exit 1; }
        curl -s -f --max-time 30 "$(u "$1")/" | tr -d '\r' || { echo "vita: cannot list $1"; exit 3; };;
    pull)
        [ -n "$2" ] || { echo "usage: vita.sh pull <remote> <local>"; exit 1; }
        pull_any "$1" "$2" || exit 3;;
    backup)
        [ -n "$1" ] || { echo "usage: vita.sh backup <TITLEID>"; exit 1; }
        d="local/backups/$(date +%Y%m%d-%H%M%S)_$1"
        mkdir -p "$d"
        if [ "$(kind "ux0:/rePatch/$1")" = d ]; then pull_any "ux0:/rePatch/$1" "$d" || exit 3
        else touch "$d/.no_repatch"; fi
        echo "$d";;
    wake)
        guard
        vc "nosleep on" >/dev/null; vc "screen on" >/dev/null
        sleep 3; unlock;;
    sleep)
        guard; vc "screen off" >/dev/null;;
    launch)
        [ -n "$1" ] || { echo "usage: vita.sh launch <TITLEID>"; exit 1; }
        guard; vc "launch $1";;
    quit)
        guard; vc "quit all";;
    push)
        [ -n "$2" ] || { echo "usage: vita.sh push <local> <remote>"; exit 1; }
        guard; push_any "$1" "$2" || exit 3;;
    rm)
        [ -n "$1" ] || { echo "usage: vita.sh rm <remote>"; exit 1; }
        guard; rm_any "$1" || exit 3;;
    restore)
        [ -d "$1" ] || { echo "usage: vita.sh restore <backup-dir>"; exit 1; }
        t=${1%/}; t=${t##*_}
        guard
        rm_any "ux0:/rePatch/$t" || exit 3
        [ -e "$1/.no_repatch" ] || push_any "$1" "ux0:/rePatch/$t" || exit 3
        echo "vita: restored ux0:/rePatch/$t from $1";;
    cmd)
        [ -n "$1" ] || { echo "usage: vita.sh cmd \"<command>\""; exit 1; }
        guard; vc "$1";;
    *)
        sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 1;;
esac
