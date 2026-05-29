#!/bin/bash
make -s || exit 1
export API_KEY=dummy
caddy reverse-proxy --from :18080 --to https://models.sjtu.edu.cn --change-host-header > /dev/null 2>&1 &
CADDY_PID=$!
./build/c-agent
kill $CADDY_PID 2>/dev/null
