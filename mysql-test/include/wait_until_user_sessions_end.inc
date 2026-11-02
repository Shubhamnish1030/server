# include file: wait until a user has no sessions left.
#
# Usage:
#   --let $wait_user= user_name
#   --source include/wait_until_user_sessions_end.inc
#
# The server ends the session of a client that is gone only if the
# disconnect_watcher is ON, so there is nothing to wait for without it.
# A test that needs the user to be gone must expect the session in that case.

--source include/disconnect_watcher_value.inc
if ($disconnect_watcher)
{
  let $wait_condition= SELECT COUNT(*)=0 FROM information_schema.processlist
    WHERE user='$wait_user';
  --source include/wait_condition.inc
}
