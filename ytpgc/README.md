# YT Cluster PostgreSQL Extension

`yt_cluster` is the PostgreSQL extension of YT Cluster. It is written in C.

- Row triggers (`yt_synchronize`) send each row change of a synchronized table
  to the node `ytserv` over a unix socket, using gRPC.
- A transaction callback asks the node to prepare, commit or roll back the
  cluster side of each transaction, so the other nodes see the same outcome.
- SQL functions (`yt_info`, `yt_merge`, `yt_llm_query`, `yt_diff_apply` and
  others) give access to the node and to the diff helpers; `yt_advisory_lock`,
  `yt_nextval` and their kin are cluster-wide locks and sequences. Their definitions
  are in `yt_cluster--1.21.sql`.

## Version

`VERSION`, at the root of the repository, is YT lite's version, in step with YT Cluster's:
`1.21.0`. The extension takes its version from it, and the two crates say the same in their
`Cargo.toml`.

The major.minor part (`1.21`) names:

- the SQL script `yt_cluster--1.21.sql`, and the `default_version` of the control file
  (`yt_cluster.control`, which `make` and `build-all-ytpgc.sh` write from
  `yt_cluster.control.in`);
- the library files, `yt_cluster1_21-pg<N>-<libc>.so` and `libyt_cluster1_21-pg<N>-<libc>.a`;
- the version compiled into the extension, which is logged when it loads and returned by
  `yt_info('T')`;
- `yt_core_proto::EXT_SQL`, the name of the script that a node's tools install
  (`../yt-core-proto/build.rs` reads the file).

`make info` prints the full version. The patch number changes nothing a database sees.

### Across versions

The extension's version is only its name: 0.5 (YT Cluster up to 1.20) and 1.21 have the same
C code and speak the same protocol to the node beside the database (`db.LocalGrpc`, the
`base` and `db` protos, unchanged). So during an upgrade a PostgreSQL may run the library of
either version under a node of either version, and a database at 0.5 works with a 1.21 node
until it is updated. The SQL differs only in comments, one message, and the spelling of one
local variable of `yt_setup`.

### A database that has an earlier version

Until 1.21 the extension had a version of its own, `0.5`. A database that has it moves to
this one with:

```sql
ALTER EXTENSION yt_cluster UPDATE;
```

`ytctl --batch` runs that. It needs the update script `yt_cluster--0.5--1.21.sql` beside the
script, where the packages put it.

The update brings the database to what `CREATE EXTENSION` makes now, one object at a time.
A function or a view is replaced where it can be. A table, an index or an event trigger is
made when it is not there. An object that cannot be changed in place is kept as it is and
named in a `WARNING`; the update does not fail on it. That covers a function whose signature
an older script declared differently, and a table that was made by hand and is no member of
the extension.

Nothing is dropped and no table is altered. A table that is already there keeps the shape it
has: when it lacks a column of this version, or has another primary key, a `WARNING` says so
and the update goes on. `yt_config` is the one that matters: an old one is keyed by `id`,
and this version's functions need the key `(module, name)`. `ytctl --batch` re-keys it; so on
a YT Cluster node update with `ytctl --batch`, not by hand.

One thing stops the update and leaves the database at its earlier version: a C function that
the library PostgreSQL has loaded does not have. That happens when the library file was
replaced and PostgreSQL still runs with the old one in memory (`shared_preload_libraries`).
Restart PostgreSQL and run the update again. `ytctl --batch` runs the update after its
restart of PostgreSQL.

The update scripts are not written by hand. `update-sql.py` makes one from the script for
every version listed in `previous-versions.txt`:

```
make update-sql          # after any change of yt_cluster--1.21.sql
make check-update-sql    # exit 1 when a script on disk is not what would be written
```

`cargo test -p yt-core-proto` fails when the script changed and the update scripts did not.

### A new major.minor

1. Add the current major.minor to `previous-versions.txt`.
2. Set the new version in `../VERSION`.
3. Rename the script to the new version, and set it in the fallback `YT_CLUSTER_VERSION` of
   `src/yt_cluster.h`.
4. `make update-sql`, and remove the update scripts that lead to the old version.

## Building

The extension builds with PGXS (`make`). You need:

- `pg_config` of the PostgreSQL version to build for
- `protoc` with protobuf-c (`protoc-gen-c`), and `libprotobuf-c`
- `libcurl`

The protocol files come from two places in this repository:
`../yt-core-proto/proto`, with a copy of diff-doc-rs's `diff_doc.proto` beside
them. With diff-doc-rs cloned next to this repository
(`git clone https://github.com/vkrinitsyn/diff-doc-rs.git`), `make` refreshes that
copy and the C sources in `src/proto` from it; without it, the committed copies
are used.

```
make
make install        # as root
```

The version of the library names and the libc tag can be set on the command
line, for example `make LIBC_SUFFIX=musl`.

### Docker builds

`ytpgc/build-all-ytpgc.sh` builds the extension for every PostgreSQL major
version listed in `ytpgc/pg-versions.txt`. Run it from the repository root:

```
bash ytpgc/build-all-ytpgc.sh
```

The artifacts are written to `ytpgc/dist`. The images are built on the Docker
daemon that `DOCKER_HOST` names; when it is not set, the local daemon is used.
Other options:

- `FORCE=1` rebuilds even when the artifacts already exist in `dist`
- `EL9=1` also builds the glibc 2.34 variant (RHEL 9 family)
- `MUSL=1` also builds the musl variant (Alpine)

## Tests

- `test.sql` checks `yt_diff_encode()` and `yt_diff_apply()` with JSON
  patches.
- `test_bson.sql` checks `yt_diff_apply_bson()`, `yt_bson_to_json()` and
  `yt_bson_from_json()`.

Both print `t` for each passing assertion. Run them in a database where the
extension is created:

```
psql -d <db> -f ytpgc/test.sql
psql -d <db> -f ytpgc/test_bson.sql
```

## Behaviour notes

- `yt_ct_decode(value)` and `yt_ct_encode(json)` convert an A row's value
  (base64 protobuf) to its JSON and back. The node does the conversion
  (`yt_info('ct-:...')`, `yt_info('ct+:...')`), so a database whose extension
  was created from an older script works too. An empty or NULL value decodes to
  the table with every default. A bad value is an error.
- **A client's own two-phase commit.** A cluster transaction is also a set of
  XA transactions, and a client's XA runs on top of it: the cluster side is
  committed before the local `COMMIT` or `COMMIT PREPARED`.
  - `PREPARE TRANSACTION '<gid>'` of a cluster-synced transaction: a
    ProcessUtility hook keeps the gid. At `XACT_EVENT_PRE_PREPARE` the extension
    asks the node to prepare the cluster side and to record the gid. If the
    node refuses, the local PREPARE fails and its abort rolls the transaction
    back on the cluster too.
  - `COMMIT PREPARED '<gid>'`, from any session: the hook first asks the node to
    commit the cluster side. If the cluster side committed or there is none, the
    local commit runs. If the cluster could not commit it, or no longer holds
    it (the node restarted since the prepare), the statement runs as
    `ROLLBACK PREPARED` and the client gets SQLSTATE 40000 with the reason. If
    the node cannot be reached, the statement is refused and the prepared
    transaction stays for the client to finish later.
  - `ROLLBACK PREPARED '<gid>'`: the cluster side is rolled back first.
  - The node's own sessions that finish their prepared transactions are not
    affected.
  - The hook runs in every backend only when `yt_cluster` is loaded through
    `shared_preload_libraries`. In a database without the extension the hook
    asks nothing, so another application's two-phase commit there never waits
    on the node.
  - With `yt_cluster.auto_complete = off` the client completes the cluster side
    itself, as for a `COMMIT`.
- **The origin's isolation level.** The extension sends the transaction's
  isolation level. A party member runs the synchronous modify at that level.
  The apply on the other nodes stays at READ COMMITTED: it replays committed
  row images in commit order. If a merge needs a row that another uncommitted
  transaction holds, the member answers 40001 and the client repeats the
  transaction.
- **A conflicting row: retry only a delta.** A row that a concurrent write
  changed is refused with 55409, "do not retry; review the row", unless the
  retry merges onto the other write. A plain UPDATE run again would overwrite
  the other writer's change unseen. A session whose statements are deltas
  (`set n = n + 1`, a JSON or text patch, made where the SQL is generated) sets
  `yt_cluster.delta = on` (default off). At the rollback point the extension
  then reports that refusal as 40001, which the drivers retry.
- **Two transactions of one node on one row.** A transaction takes its subterm
  at its first statement, and every peer applies transactions in subterm order.
  If a transaction with the higher subterm writes a row and commits first, and a
  transaction with the lower subterm then writes the same row, the origin refuses
  the later write with 40001 and records nothing of it. The client retries the
  transaction, which then gets a subterm above the first one. This does not apply
  to a merge table, which is compared path by path. Without this rule the peers
  applied the lower subterm first, the compare-and-set missed the row, and the
  copies stayed apart.

## Cluster locks and sequences

PostgreSQL's advisory locks and `nextval()`, across the cluster (`src/ycoord.c`; the node
side is ytserv's `state/ycoord.rs`, decided by its etcd's leader):

| function | what |
|---|---|
| `yt_advisory_lock(key bigint)` | waits until this session holds the cluster lock; held again, once more |
| `yt_try_advisory_lock(key bigint) -> bool` | the lock now, or false while another holds it or waits for it |
| `yt_advisory_unlock(key bigint) -> bool` | one hold let go; false, with a warning, when none was held |
| `yt_advisory_xact_lock(key bigint)` | as `yt_advisory_lock`, until the transaction ends |
| `yt_nextval(seq regclass) -> bigint` | the next number of `seq`'s cluster sequence: a column default as `nextval('seq')` is |
| `yt_create_sequence(seq regclass, cache int = 1, if_missing bool = false) -> bigint` | the cluster sequence of the local sequence `seq`, above its last value; each node takes `cache` numbers at once. Its owner's |
| `yt_cluster_sequences(rel regclass, cluster bool = true) -> int` | a table's column defaults `nextval('s')` to `yt_nextval('s')` (each one's cluster sequence made above the column's largest value), or back: the local sequence moved above every number the cluster's gave. On this node - run it on each, as `yt_setup`; `ytctl --cluster-sequences on|off` does it for the synced tables |

- **Locks** are per database, as PostgreSQL's are, and held by the backend (its pid and
  start time). A blocking lock asks again about each second, checking for a cancel in
  between: query cancel and `statement_timeout` work, and a wait given up leaves the queue.
  A transaction's locks go at its end, a session's at the backend's exit; a rolled-back
  subtransaction lets go of the transaction locks it took. ytserv frees the locks of a
  backend `pg_stat_activity` no longer shows, and a PostgreSQL restart frees every lock of
  the node.
- **A holder never outlives its lock.** The node's locks are held under one lease of the
  lock timeout (etcds' `lock_ttl`, 60 s): when ytserv cannot keep it alive for nearly that
  long, or finds it gone, it ends the backends holding cluster locks before the locks can
  go to another node - their transactions roll back.
- **PREPARE TRANSACTION** is refused while the transaction holds cluster transaction locks
  (`yt_advisory_xact_lock`): PostgreSQL would carry them to the prepared transaction, and a
  cluster lock is the backend's.
- **Sequences**: the local sequence is the cluster sequence's twin. With each number the
  twin's last value goes along, and one past what the cluster gave - numbers the twin gave
  while the cluster was off - moves the cluster sequence above it first: nothing repeats.
  `yt_setup` switches a table's defaults when its config says `cluster_sequence` (or the C
  setting `def_cluster_sequence`, off by default, for every table). Back to its own, a
  table's cluster sequence is dropped, its mark kept: a node still on it is refused its next
  block rather than given numbers twice - switch every node.
- **The cluster off** for a session (`SET yt.node = 'local'`): the locks are PostgreSQL's own
  advisory locks, and `yt_nextval` is the twin's `nextval()`.
- A database installed before these functions gets them from ytserv at its load.

## Savepoints

A member holds a transaction's rows and only commits or rolls back all of them:
nothing undoes one write there. So a transaction that rolled a savepoint back
over a write it sent to the cluster, including a PL/pgSQL `EXCEPTION` block,
cannot commit. `COMMIT`, `yt_complete(true)` and `PREPARE TRANSACTION` fail with
SQLSTATE `0A000`, and the transaction is on no node. Retry it without rolling
back over a write. A savepoint rolled back over no write, such as a duplicate
key caught before it was sent, commits as before. A write that the cluster
refused counts too, since it may have reached some members. A write to a table
listed in `ignore` reached no node, and does not count. A rollback after
`yt_complete(true)` has already committed the cluster is a separate case.
