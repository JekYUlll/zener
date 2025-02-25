# shellcheck disable=SC2046
# shellcheck disable=SC2009
kill -9 $(ps -ef | grep 'Zener' | grep -v grep | awk '{print $2}')