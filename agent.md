# agent.md - yt-cluster-lite

A guide for coding agents (and people) who use or change this repository: the PostgreSQL
extension (`ytpgc/`) and the lite node crates.

## The cluster OFF switch: `yt.node`

The extension reads the PostgreSQL setting `yt.node`: the node's uuid in the cluster,
written into PostgreSQL's configuration when the node is set up (`conf.d/yt.conf`). The value
`local` turns the cluster off: the extension then skips every cluster call - the commit and
two-phase commit hooks, the row triggers' capture, and the rest
(`yt_cluster_is_endless_loop()` in `ytpgc/src/yt_cluster.c`) - and PostgreSQL works as a
single server of its own.

- **One session, at once:** `SET yt.node = 'local';` - until the session ends, or
  `RESET yt.node`; `SET LOCAL yt.node = 'local'` for one transaction. ytserv's own
  transactions that apply other nodes' rows set it so: it is also the guard that keeps an
  applied row from being captured and sent back.
- **The whole instance, kept:** a restart of PostgreSQL takes the value from the
  configuration file again, so the switch that lasts is made there. Comment the old line
  out - keep it, to switch back - and add `local` below it:

  ```
  # yt.node = '<the node uuid>'
  yt.node = 'local'
  ```

  then restart PostgreSQL. To turn the cluster back on: the old line uncommented, the
  `local` one removed, PostgreSQL restarted. (A reload applies a new `yt.node` to every
  backend as well; the restart is the sure way.)
- **Cluster locks and sequences** (`yt_advisory_lock` and its kin, `yt_nextval`; ytpgc/README.md):
  with the switch on, a session's locks are PostgreSQL's own advisory locks and it takes
  numbers from the sequence's local twin, as `nextval()` does; back on, the cluster
  sequence moves above what the twin gave.
