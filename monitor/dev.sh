#!/usr/bin/env bash
exec concurrently -n vite,serial -c blue,green "vite" "tsx server.ts ${1:-}"
