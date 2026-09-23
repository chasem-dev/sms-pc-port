#!/bin/sh
# Run build/sms under gdb and print a backtrace at the first fatal signal.
cd "$(dirname "$0")/../${SMS_BUILD:-build-gx}" && timeout ${T:-120} gdb -q -batch -ex "set debuginfod enabled off" -ex "handle SIGSEGV stop" -ex "handle SIG34 nostop noprint" -ex run -ex "bt ${BT:-15}" -ex "info locals" --args ./sms "$@" 2>&1 | grep -v "^\[New Thread\|^\[Thread\|libthread_db\|^Using host"
