#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
rid="${1:-osx-x64}"
case "$rid" in osx-x64|win-x64) ;; *) echo 'Use osx-x64 or win-x64' >&2; exit 1;; esac
dotnet run --project tests/Tests.csproj -p:NuGetAudit=false -- examples/options.json
dotnet publish src/Client/DDXModMenu.Client.csproj -c Release -r "$rid" -p:NuGetAudit=false -o "artifacts/$rid"
# Deployment is explicit: artifacts depend on the proposed DDX extension.
