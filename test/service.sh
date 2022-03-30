#!/bin/bash
#
# Dummy init script.
#

service=$(basename "$0")
command="$1"
expect_file="test-init-${service}-${command}-outcome"
witness_file="test-init-${service}-${command}-witness"

if [ -f "${expect_file}" ]; then
    read -r outcome <"${expect_file}"
else
    outcome=0
fi
pwd >&2
echo "${service} ${command} ${outcome}" >&2

case "${command}" in
start | stop | restart | reload)
    message="${command}ing ${service}"
    ;;
status)
    case "${outcome}" in
    0)
        message="${service} is running"
        ;;
    3)
        message="${service} is stopped"
        ;;
    4)
        message="${service} is dead"
        ;;
    *)
        outcome=1
        ;;
    esac
    ;;
*)
    outcome=1
    message="unrecognized command '${command}'"
    ;;
esac

echo "${message}"
echo "${outcome}" >"${witness_file}"
echo "${message}" >>"${witness_file}"
exit "${outcome}"
