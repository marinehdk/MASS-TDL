#!/bin/bash

# MASS L3 SIL Manager for OrbStack
# Handles startup, health checks, and automated verification.

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}>>> Initializing MASS L3 SIL Stack via OrbStack...${NC}"

# 1. Check OrbStack/Docker status
if ! docker info > /dev/null 2>&1; then
    echo -e "${RED}[ERROR] OrbStack / Docker is not running. Please start it first.${NC}"
    exit 1
fi

# 2. Build and Start Containers
echo -e ">>> Starting services (bridge, mock, hmi)..."
docker-compose up -d --build

# 3. Wait for Bridge (8765) and HMI (8000)
echo -n ">>> Waiting for Foxglove Bridge (8765)..."
retry_bridge=0
until $(curl --output /dev/null --silent --head --fail http://localhost:8765); do
    if [ "$retry_bridge" -gt 120 ]; then echo -e "${RED} Timeout!${NC}"; exit 1; fi
    printf '.'
    sleep 2
    retry_bridge=$((retry_bridge+1))
done
echo -e " ${GREEN}READY${NC}"

echo -n ">>> Waiting for Frontend HMI (8000)..."
retry_hmi=0
until $(curl --output /dev/null --silent --head --fail http://localhost:8000); do
    if [ "$retry_hmi" -gt 120 ]; then echo -e "${RED} Timeout!${NC}"; exit 1; fi
    printf '.'
    sleep 1
    retry_hmi=$((retry_hmi+1))
done
echo -e " ${GREEN}READY${NC}"

# 4. Automated Verification
echo -e ">>> Running Playwright Verification..."
cd web && URL=http://localhost:8000 node scripts-playwright-verify.mjs

echo -e "${GREEN}>>> SIL Stack is fully operational and verified.${NC}"
