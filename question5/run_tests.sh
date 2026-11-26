#!/usr/bin/env bash
set -Eeuo pipefail

SERVER_PID=""
CLIENT_PIDS=()
EVIDENCE_DIR="evidence"

mkdir -p "${EVIDENCE_DIR}"

cleanup() {
    if [[ -n "${SERVER_PID}" ]] &&
       kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill -INT "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
}

trap cleanup EXIT

echo " Compile server and client "

rm -f server client
rm -f "${EVIDENCE_DIR}"/registrations.db \
    "${EVIDENCE_DIR}"/server.log \
    "${EVIDENCE_DIR}"/client_*.log \
    "${EVIDENCE_DIR}"/invalid_auth.log \
    "${EVIDENCE_DIR}"/unexpected_disconnect.log

gcc -Wall -Wextra -Werror -pedantic -std=c11 \
    -pthread server.c -o server

gcc -Wall -Wextra -Werror -pedantic -std=c11 \
    client.c -o client

echo "Compilation succeeded with strict warnings enabled"

echo
echo " Start server "

(
    cd "${EVIDENCE_DIR}"
    exec ../server > server.log 2>&1
) &
SERVER_PID=$!

sleep 1

if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    echo "Server failed to start. See evidence/server.log."
    cat "${EVIDENCE_DIR}/server.log"
    exit 1
fi

echo
echo " Unsuccessful authentication "

printf 'INVALID_ID\n' | ./client |
    tee "${EVIDENCE_DIR}/invalid_auth.log"

echo
echo " Successful authentication, event listing, registration & duplicate rejection "

printf 'STU001\n1\n102\n1\n102\n2\n' |
    ./client | tee "${EVIDENCE_DIR}/client_1.log"

echo
echo " Six clients compete for remaining places in Event 102 "

for i in 2 3 4 5 6 7; do
    printf 'STU00%d\n1\n102\n2\n' "$i" |
        ./client > "${EVIDENCE_DIR}/client_${i}.log" &
    CLIENT_PIDS+=("$!")
done

for pid in "${CLIENT_PIDS[@]}"; do
    wait "${pid}"
done

CLIENT_PIDS=()

for i in 2 3 4 5 6 7; do
    echo "--- STU00${i} ---"
    grep -E 'SUCCESS|REJECTED|Goodbye' \
        "${EVIDENCE_DIR}/client_${i}.log" || true
done

echo
echo " Verify event 102 did not exceed capacity "

SUCCESS_COUNT=$(
    grep -hF "Successfully registered for Cybersecurity Seminar" \
        "${EVIDENCE_DIR}"/client_{1..7}.log | wc -l
)

FULL_COUNT=$(
    grep -hF "REJECTED - FULL" \
        "${EVIDENCE_DIR}"/client_{2..7}.log | wc -l
)

DB_COUNT=$(grep -c '^102,' "${EVIDENCE_DIR}/registrations.db" || true)

echo "Successful registrations across STU001-STU007: ${SUCCESS_COUNT}"
echo "Full-event rejections among concurrent clients: ${FULL_COUNT}"
echo "Database registrations for Event 102: ${DB_COUNT}"

if [[ "${SUCCESS_COUNT}" -ne 4 ]]; then
    echo "FAIL: expected 4 registrations for Event 102"
    exit 1
fi

if [[ "${FULL_COUNT}" -ne 3 ]]; then
    echo "FAIL: expected 3 full-event rejections"
    exit 1
fi

if [[ "${DB_COUNT}" -ne 4 ]]; then
    echo "FAIL: database should contain exactly 4 Event 102 registrations"
    exit 1
fi

echo "PASS: Event 102 capacity enforcement verified (4/4)"

echo
echo " Unexpected client disconnection "

./client < <(printf 'STU008\n'; sleep 30) \
    > "${EVIDENCE_DIR}/unexpected_disconnect.log" 2>&1 &

UNEXPECTED_CLIENT_PID=$!

sleep 2

kill -KILL "${UNEXPECTED_CLIENT_PID}" 2>/dev/null || true
wait "${UNEXPECTED_CLIENT_PID}" 2>/dev/null || true

sleep 1

if grep -q 'DISCONNECT.*STU008' "${EVIDENCE_DIR}/server.log"; then
    echo "PASS: Server detected the unexpected client disconnection"
else
    echo "WARNING: Unexpected disconnection was not confirmed in server.log"
fi

echo
echo " Graceful server shutdown "

kill -INT "${SERVER_PID}"
wait "${SERVER_PID}" || true
SERVER_PID=""

echo
echo " Server audit log "

cat "${EVIDENCE_DIR}/server.log"

echo
echo " Persisted registration database "

cat "${EVIDENCE_DIR}/registrations.db"

echo
echo " Test summary "

echo "Expected Event 102 final state: 4/4"
echo "Expected Event 102 registrations: STU001 plus three of STU002-STU007"

