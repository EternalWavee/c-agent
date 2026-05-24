#!/bin/bash
caddy reverse-proxy --from :18080 --to https://models.sjtu.edu.cn --change-host-header &
CADDY_PID=$!
./build/c-agent
kill $CADDY_PID 2>/dev/null
