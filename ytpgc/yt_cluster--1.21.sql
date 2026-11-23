-- SPDX-License-Identifier: MIT - Copyright (c) 2026 DBinvent
-- yt_cluster extension SQL
-- YT Cluster PostgreSQL Extension - C implementation

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION yt_cluster" to load this file. \quit

-- Trigger function: yt_synchronize
-- Called by BEFORE triggers on synchronized tables
CREATE FUNCTION yt_synchronize()
    RETURNS trigger
    STRICT
    SET search_path TO @extschema@
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_synchronize';

-- yt_complete(is_commit bool)
-- Notify YT server about transaction commit/rollback
CREATE FUNCTION yt_complete(is_commit bool)
    RETURNS int
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_complete';

-- yt_info(input text)
-- Get cluster info with input filter
CREATE FUNCTION yt_info(input text)
    RETURNS json
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_info';

-- yt_info()
-- Get cluster info (default)
CREATE FUNCTION yt_info()
    RETURNS json
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_info_def';

-- yt_info(term int, subterm int)
-- Get info for specific term/subterm
CREATE FUNCTION yt_info(term bigint, subterm bigint)
    RETURNS json
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_info_data';

-- yt_info(term int, subterm int, version int)
-- Get info for specific term/subterm/version
CREATE FUNCTION yt_info(term bigint, subterm bigint, version bigint)
    RETURNS json
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_info_data_version';

-- yt_merge(term int, subterm int)
-- Pull data from cluster for term/subterm
CREATE FUNCTION yt_merge(term bigint, subterm bigint)
    RETURNS json
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_merge';

-- yt_merge(term int, subterm int, force_commit bool)
-- Pull data from cluster with optional force commit
CREATE FUNCTION yt_merge(term bigint, subterm bigint, force_commit bool)
    RETURNS json
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_merge_force';

-- yt_merge()
-- Pull all data from cluster
CREATE FUNCTION yt_merge()
    RETURNS json
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_merge_def';

-- yt_s3_blob(): the BEFORE row trigger of a blob column in `move` or `quick`: ytserv
-- puts it on the table. Its arguments: the directory, then each column and its mode.
CREATE FUNCTION yt_s3_blob()
    RETURNS trigger
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_s3_blob';

-- yt_s3_probe(dir text): a test file a backend wrote, for ytserv to read: are they
-- looking at one directory?
CREATE FUNCTION yt_s3_probe(dir text)
    RETURNS text
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_s3_probe';
REVOKE ALL ON FUNCTION yt_s3_blob() FROM PUBLIC;
REVOKE ALL ON FUNCTION yt_s3_probe(text) FROM PUBLIC;

-- [ycoord] Cluster-wide advisory locks and sequences, as pg_advisory_lock() and nextval()
-- across the cluster: held on the node's ytserv, decided by its etcd's leader. The cluster
-- off for the session (yt.node = 'local'): PostgreSQL's own advisory locks, and the local
-- sequence's nextval().
-- yt_advisory_lock(key): waits until this session holds it; held again, once more
CREATE FUNCTION yt_advisory_lock(key bigint)
    RETURNS void
    LANGUAGE c STRICT VOLATILE
    AS 'MODULE_PATHNAME', 'yt_advisory_lock';
-- yt_try_advisory_lock(key): the lock now, or false
CREATE FUNCTION yt_try_advisory_lock(key bigint)
    RETURNS boolean
    LANGUAGE c STRICT VOLATILE
    AS 'MODULE_PATHNAME', 'yt_try_advisory_lock';
-- yt_advisory_unlock(key): one hold let go; false, with a warning, when none was held
CREATE FUNCTION yt_advisory_unlock(key bigint)
    RETURNS boolean
    LANGUAGE c STRICT VOLATILE
    AS 'MODULE_PATHNAME', 'yt_advisory_unlock';
-- yt_advisory_xact_lock(key): as yt_advisory_lock, until the transaction ends
CREATE FUNCTION yt_advisory_xact_lock(key bigint)
    RETURNS void
    LANGUAGE c STRICT VOLATILE
    AS 'MODULE_PATHNAME', 'yt_advisory_xact_lock';
-- yt_nextval(seq): the next number of seq's cluster sequence (yt_create_sequence); a column
-- default as nextval('seq') is
CREATE FUNCTION yt_nextval(seq regclass)
    RETURNS bigint
    LANGUAGE c STRICT VOLATILE
    AS 'MODULE_PATHNAME', 'yt_nextval';
-- yt_create_sequence(seq, cache, if_missing): the cluster sequence of the local sequence seq,
-- above its last value, each node taking cache numbers at once; the first number it hands
-- out. There already: refused - or with if_missing moved above this node's seq, its next.
CREATE FUNCTION yt_create_sequence(seq regclass, cache integer DEFAULT 1, if_missing boolean DEFAULT false)
    RETURNS bigint
    LANGUAGE c VOLATILE
    AS 'MODULE_PATHNAME', 'yt_create_sequence';
-- yt_cluster_sequences(rel, cluster): a table's sequences switched to the cluster's (true) or
-- back to its own (false), on this node - run on each, as yt_setup is. Each column default
-- nextval('s') becomes yt_nextval('s'), its cluster sequence made when missing, above the
-- column's largest value; back, the local sequence is moved above every number the
-- cluster's handed out and the cluster's dropped - a node still on it is refused, not given
-- numbers twice: switch every node. The number of columns switched. yt_setup runs it for a table
-- whose config says cluster_sequence (or def_cluster_sequence for all).
CREATE FUNCTION yt_cluster_sequences(rel regclass, cluster boolean DEFAULT true)
    RETURNS integer
    LANGUAGE plpgsql
AS $ycoord$
DECLARE
    r       record;
    n       integer := 0;
    top     bigint;
    seqname text;
    nxt     bigint;
BEGIN
    -- [ycoord] each column whose default draws on a sequence (nextval or yt_nextval): an
    -- identity column has no default of the kind, and is left as it is
    FOR r IN SELECT a.attname, dep.refobjid::regclass AS seq, pg_get_expr(d.adbin, d.adrelid) AS expr
             FROM pg_attrdef d
             JOIN pg_attribute a ON a.attrelid = d.adrelid AND a.attnum = d.adnum AND NOT a.attisdropped
             JOIN pg_depend dep ON dep.classid = 'pg_attrdef'::regclass AND dep.objid = d.oid
                               AND dep.refclassid = 'pg_class'::regclass
             JOIN pg_class s ON s.oid = dep.refobjid AND s.relkind = 'S'
             WHERE d.adrelid = rel
             ORDER BY a.attnum LOOP
        SELECT n2.nspname || '.' || c2.relname INTO seqname
            FROM pg_class c2 JOIN pg_namespace n2 ON n2.oid = c2.relnamespace WHERE c2.oid = r.seq;
        IF cluster AND r.expr NOT LIKE '%yt_nextval(%' THEN
            -- the local sequence above the column's largest value, so the cluster's starts above both
            EXECUTE format('SELECT max(%I)::bigint FROM %s', r.attname, rel) INTO top;
            IF top IS NOT NULL AND top > coalesce(pg_sequence_last_value(r.seq), top - 1) THEN
                PERFORM setval(r.seq, top, true);
            END IF;
            PERFORM @extschema@.yt_create_sequence(r.seq, 1, true);
            EXECUTE format('ALTER TABLE %s ALTER COLUMN %I SET DEFAULT @extschema@.yt_nextval(%L::regclass)',
                rel, r.attname, r.seq::text);
            n := n + 1;
        ELSIF NOT cluster AND r.expr LIKE '%yt_nextval(%' THEN
            -- the local sequence above every number the cluster's handed out, and the cluster's
            -- dropped, its mark kept: a node still on it is refused its next block rather than
            -- handed numbers this one's local sequence gives now
            SELECT (x ->> 'value')::bigint INTO nxt
                FROM json_array_elements(@extschema@.yt_info('sq-:' || octet_length(current_database()) || '|'
                    || current_database() || seqname)) x LIMIT 1;
            IF nxt IS NOT NULL AND nxt - 1 > coalesce(pg_sequence_last_value(r.seq), nxt - 2) THEN
                PERFORM setval(r.seq, nxt - 1, true);
            END IF;
            EXECUTE format('ALTER TABLE %s ALTER COLUMN %I SET DEFAULT nextval(%L::regclass)',
                rel, r.attname, r.seq::text);
            n := n + 1;
        END IF;
    END LOOP;
    RETURN n;
END
$ycoord$;
-- a table's defaults are DDL: the owner's or a superuser's to grant
REVOKE ALL ON FUNCTION yt_cluster_sequences(regclass, boolean) FROM PUBLIC;

-- yt_ct_encode(input text)
-- A table config as JSON - the form yt_ct_decode prints - to the base64 protobuf
-- an A row holds; ytserv converts it (yt_info('ct+:...')). ytctl --encode-table-cfg
-- takes the k=v spec form instead.
CREATE FUNCTION yt_ct_encode(input text)
    RETURNS text
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_ct_encode';

-- yt_ct_decode(input text)
-- An A row's value (base64 protobuf) to its JSON, an empty value being every
-- default; ytserv converts it (yt_info('ct-:...')).
CREATE FUNCTION yt_ct_decode(input text)
    RETURNS text
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_ct_decode';

-- yt_diff_apply(doc jsonb, diff bytea)
-- Decode a protobuf-encoded DiffDoc::Mismatches and apply it to a jsonb
-- document, returning the patched jsonb.
-- The bytea argument must be a wire-format protobuf of diff_doc.Mismatches
-- as defined in diff.proto (doc, text, or patch variant).
CREATE FUNCTION yt_diff_apply(doc jsonb, diff bytea)
    RETURNS jsonb
    STRICT
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_diff_apply';

-- yt_diff_encode(diff jsonb)
-- Encode a DiffDoc::Mismatches expressed as proto3 JSON into protobuf wire
-- format (bytea).  The result is suitable for passing to yt_diff_apply().
-- JSON shape (mirrors proto field names):
--   {"doc":   {"hunks": [{"path": [{"name":"f"}|{"idx":0}],
--                         "value": {"update"|"insert":{<GenericValue>}
--                                  |"remove":true|"swap"|"clone":{…}}}]}}
--   {"patch": "<gnu-patch-text>"}
CREATE FUNCTION yt_diff_encode(diff jsonb)
    RETURNS bytea
    STRICT
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_diff_encode';

-- yt_diff_apply_bson(doc bytea, diff bytea)
-- yt_diff_apply for a BSON document (DocumentDB / MongoDB) as bytea: the
-- patched document's BSON, its keys sorted and its _id first - the same bytes
-- on each node. Its types beyond JSON's are paths of extended JSON: a date's
-- hunk is at ["at", "$date"], an int64 of int32's range at ["n", "$numberLong"].
CREATE FUNCTION yt_diff_apply_bson(doc bytea, diff bytea)
    RETURNS bytea
    STRICT
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_diff_apply_bson';

-- yt_bson_to_json(doc bytea) / yt_bson_from_json(doc jsonb)
-- A BSON document as relaxed extended JSON - {"$oid": ...}, {"$date": ...},
-- {"$numberDecimal": ...} - and back, to the BSON type each was.
CREATE FUNCTION yt_bson_to_json(doc bytea)
    RETURNS jsonb
    STRICT IMMUTABLE
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_bson_to_json';

CREATE FUNCTION yt_bson_from_json(doc jsonb)
    RETURNS bytea
    STRICT IMMUTABLE
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_bson_from_json';

-- ─── Tables the extension owns ──────────────────────────────────────────────
--
-- Created here rather than by `ytctl`, because the extension must be created in
-- EVERY replicated database anyway: CREATE EXTENSION is database-scoped and the
-- yt_synchronize() triggers are table-scoped. So every database that can
-- replicate at all now gets its config and frame tables with no extra step and
-- nothing for an operator to forget.
--
-- Note: DROP EXTENSION takes these with it. They are this database's replication
-- state.

-- Per-table synchronization config (module 'A'), node/cluster config ('C','N',
-- 'K','Z','G'), one row per setting of an S3 store ('B'), of Schema Guard ('M'), the
-- databases to replicate ('U'), small maps ('F') and the rest. `name` holds
-- `schema.table` for module 'A'; the
-- database is added by the server when it loads the row, so it must NOT appear
-- here (see yt_setup below). `value` is text because module 'K' holds PEM
-- certificates and keys (~600 bytes, see below).
-- Keyed by the setting, (module, name): a second insert of one setting is that setting written again, not a row of
-- its own, and no node's local insert can take an id the leader's next row needs.
-- The id stays, a plain column, for the order the loader reads in. A table
-- installed before is re-keyed by `ytctl --batch`.
CREATE TABLE IF NOT EXISTS @extschema@.yt_config (
    id     serial,
    name   varchar(80)  NOT NULL,
    module char         NOT NULL DEFAULT ' ',
    value  text         NOT NULL,
    PRIMARY KEY (module, name)
);

-- pg_dump must carry the CONTENTS, not just an empty shell: without this a
-- dump/restore silently loses every table registration and cluster setting.
SELECT pg_catalog.pg_extension_config_dump('@extschema@.yt_config', '');

-- Local frame cache: per-range row count and checksums for background
-- reconciliation.
--
-- LOCAL ONLY and must stay that way - never give this table a yt_synchronize
-- trigger and never register it in module 'A'. Replicating it would mean
-- computing frames replicates frames. Losing it costs exactly one recompute,
-- which is why it needs no WAL, no backup and no consensus - and why its
-- contents are deliberately NOT in pg_extension_config_dump above.
CREATE TABLE IF NOT EXISTS @extschema@.yt_stat (
    schema   text        NOT NULL,
    tabl     text        NOT NULL,
    level    smallint    NOT NULL,          -- 0 = coarse, n = descended
    lo       bigint      NOT NULL,
    hi       bigint      NOT NULL,
    cnt      bigint      NOT NULL,
    xor32    int         NOT NULL,
    sum64    bigint      NOT NULL,
    max_ts   timestamptz,                   -- when the table declares a last-update column
    computed timestamptz NOT NULL DEFAULT now(),
    stale    boolean     NOT NULL DEFAULT false,
    PRIMARY KEY (schema, tabl, level, lo)
);

-- The one place ytserv reports an operational problem.
--
-- Critical and blocked rows from recon, and WARN and CRIT from health, are
-- recorded here and not only in the log. Both ask the same question of an
-- operator ("something needs attention, here is what and why"), so one table
-- serves both.
--
-- A table rather than NOTIFY or a queue: it survives a restart, it is readable
-- by whatever monitoring the operator already runs, and it needs no other
-- service. NOTIFY is ephemeral (only sessions listening at that instant).
--
-- Node-local and NOT replicated: an alert is about THIS node's view. The same
-- divergence seen from three nodes is three rows, which is what you want when
-- deciding which node is wrong.
CREATE TABLE IF NOT EXISTS @extschema@.yt_alert (
    id       bigserial   PRIMARY KEY,
    ts       timestamptz NOT NULL DEFAULT now(),
    class    smallint    NOT NULL,          -- 2 = warning, 3 = critical
    source   text        NOT NULL,          -- 'recon' | 'health' | 'config' | 'meter'
    subject  text        NOT NULL,          -- 'db.schema.table', or the metric
    detail   text        NOT NULL,
    -- the row this is about, when there is one; null for a node-level alert
    pk       text,
    -- set when the condition clears, so history is kept rather than deleted
    resolved timestamptz
);
CREATE INDEX IF NOT EXISTS yt_alert_open_idx
    ON @extschema@.yt_alert (source, subject) WHERE resolved IS NULL;

-- Runtime topology jobs: a table's move, az, cleanup, split and combine. One row per job, SYNCED like any replicated table
-- (setup registers it: an 'A' row and the yt_synchronize trigger), so a job asked
-- for on any node reaches the leader, and the leader's decisions reach every
-- node. Ask by inserting a row; cancel by setting state = 'cancel'. Every other
-- state is written by the cluster leader alone:
--   requested -> approved | refused;  approved -> running -> done | failed;
--   requested | approved | running -> cancel -> cancelled.
-- The id is the request time in microseconds, so ids from different nodes
-- neither collide in practice nor need a shared sequence.
CREATE TABLE IF NOT EXISTS @extschema@.yt_job (
    id           bigint      PRIMARY KEY
                             DEFAULT (extract(epoch from clock_timestamp()) * 1000000)::bigint,
    kind         text        NOT NULL,              -- move | noop | cleanup | az_create | ...
    tabl         text,                              -- <db>.<schema>.<table>, when it has one
    scope        text        NOT NULL DEFAULT 'cluster',
    params       text        NOT NULL DEFAULT '',   -- key=value;flag;...
    state        text        NOT NULL DEFAULT 'requested',
    reason       text        NOT NULL DEFAULT '',   -- why refused / failed / done
    requested_by text        NOT NULL DEFAULT current_user,
    approved_by  text        NOT NULL DEFAULT '',   -- the approving leader's uuid
    created      timestamptz NOT NULL DEFAULT now(),
    updated      timestamptz NOT NULL DEFAULT now(),
    history      text        NOT NULL DEFAULT ''    -- one line per transition
);
SELECT pg_catalog.pg_extension_config_dump('@extschema@.yt_job', '');

-- ─── yt_llm: plain-language questions in, rows out ───────────────────
--
--   SET search_path = shop;        -- the tables the question is about
--   SELECT * FROM yt_llm('How many customers are there in each country?')
--       AS t(country text, customers bigint);
--   SELECT yt_llm_query('How many customers are there in each country?');         -- the SQL
--   SELECT yt_llm_query('How many customers are there in each country?', 0, 10);  -- rows 1-10
--
-- yt_llm_query() calls this node's ytserv, which does all of the model call:
-- it describes the tables of search_path the current role may read (catalog
-- only, no sample values), asks the text-to-SQL service over
-- mutual TLS, and answers with the statement - or, for a page, with its rows,
-- read as the current role.
--
-- The node may answer from ClickHouse or from DataFusion
-- over the cluster's tables instead: it goes by the tables the model's
-- statement reads. yt_llm() then pages the rows out of the node, and the node
-- runs the statement it already has, without asking the model again.
--
-- No time limit on either: the model thinks for minutes on a CPU, an analytic
-- statement may run for many more. A cancel of the statement (or its
-- statement_timeout) ends it - and yt_llm_query()'s call to the node, which
-- then hangs up on the model and cancels the page it runs.
--
-- ytserv is configured in the config table:
--   module 'C'  serve_llm = true, llm_host = <host:port>,
--               llm_ca / llm_cert / llm_key = NAMES of module 'K' rows
--   module 'K'  those rows, encrypted with the node's app key (marg: SECRET, or
--               --key <file>), passphrase = hex of that key:
--     key      openssl pkcs8 -topk8 -v2 aes-256-cbc -in <client>.key -passout env:PASS
--     certs    openssl enc -aes-256-cbc -pbkdf2 -iter 100000 -salt -a -A -in <x>.crt -pass env:PASS
--
-- Everything between the two [llm] lines can be re-run, so it installs into a
-- database created before it existed (a test takes the block by these lines).
-- [llm] begin

-- yt_llm_query(nl, page, size): the node's answer to a question (llm.c):
--   {"type":"sql","sql":"SELECT ..."}, {"type":"ch","sql":...,"notes":[...]},
--   {"type":"df","sql":...,"script":...,"plan":{...},"notes":[...]}, or, when page
--   or size is given, {"type":"data","engine":"sql|ch|df","data":[{...}],
--   "column_names":[...],"column_desc":[...],"page":0,"page_size":10,"total_rows":9}
-- Not for PUBLIC: GRANT EXECUTE to the roles that may use the model.
CREATE OR REPLACE FUNCTION @extschema@.yt_llm_query(nl text, page int DEFAULT NULL, size int DEFAULT NULL)
    RETURNS json
    LANGUAGE c VOLATILE
    AS 'MODULE_PATHNAME', 'yt_llm_query';
REVOKE ALL ON FUNCTION @extschema@.yt_llm_query(text, int, int) FROM PUBLIC;

-- yt_llm(nl): the rows of the answer. A statement is run here, as the caller; a
-- page of data becomes rows, typed by its column_desc. The shape is the model's,
-- so the caller names it: SELECT * FROM yt_llm('...') AS t(col type, ...).
-- A ClickHouse or DataFusion statement runs where the node chose: its
-- rows come from the node, 10000 a page.
--
-- The statement runs read-only. transaction_read_only cannot go in the
-- function's SET clause (PostgreSQL refuses it there), and SET LOCAL would stay
-- for the rest of the caller's transaction - so it is set inside the inner
-- block, and that block's subtransaction is rolled back on purpose once the
-- rows are out: the rollback takes the setting back, the rows stay, since
-- plpgsql keeps its result outside subtransactions.
CREATE OR REPLACE FUNCTION @extschema@.yt_llm(nl text)
    RETURNS SETOF record
    LANGUAGE plpgsql VOLATILE STRICT
AS $$
DECLARE
    r json := @extschema@.yt_llm_query(nl);
    p int := 0;
BEGIN
    IF r->>'type' IN ('ch', 'df') THEN
        RAISE NOTICE 'yt_llm (%): %', r->>'type', r->>'sql';
        r := @extschema@.yt_llm_query(nl, 0, 10000);
    END IF;
    IF r->>'type' = 'data' THEN
        LOOP
            RETURN QUERY EXECUTE format('SELECT * FROM json_to_recordset($1) AS x(%s)',
                (SELECT string_agg(format('%I %s', n, t), ', ' ORDER BY i)
                   FROM json_array_elements_text(r->'column_names') WITH ORDINALITY AS a(n, i)
                   JOIN json_array_elements_text(r->'column_desc') WITH ORDINALITY AS b(t, j) ON i = j))
                USING r->'data';
            p := p + 1;
            EXIT WHEN p::bigint * (r->>'page_size')::bigint >= (r->>'total_rows')::bigint;
            r := @extschema@.yt_llm_query(nl, p, 10000);
        END LOOP;
        RETURN;
    END IF;

    RAISE NOTICE 'yt_llm: %', r->>'sql';
    BEGIN
        PERFORM set_config('transaction_read_only', 'on', true);
        RETURN QUERY EXECUTE r->>'sql';
        RAISE SQLSTATE 'YTLLM';
    EXCEPTION WHEN SQLSTATE 'YTLLM' THEN
        NULL;
    END;
END
$$;

-- [llm] end

-- ─── Setup helpers ──────────────────────────────────────────────────────────
--
-- yt_setup() and two views.

-- Show table syncronization configuration taken from yt_config
-- use yt_ct_encode to write it back
create view yt_tables_cfg as select name, yt_ct_decode(value) from @extschema@.yt_config where module='A';


-- Configuration Defaults with short description
CREATE VIEW yt_help_with_defaults AS
	WITH default_values AS (
	SELECT * FROM json_to_recordset(@extschema@.yt_info('D')) AS x(name text, module text, value text)
), help as (
	SELECT * FROM json_to_recordset(@extschema@.yt_info('H')) AS x(name text, module text, value text)
)
SELECT t1.name, t1.module, t1.value, t2.value as description
FROM
    default_values t1
        JOIN
    help t2
    -- a setting of C is module S here and H there; a row of a module of its own (B, M, U, F) is
    -- that module's in both, and a name may be in two of them
    ON t1.name = t2.name AND (t1.module = t2.module OR (t1.module = 'S' AND t2.module = 'H'))
;

-- The combined config as this database sees it: every C setting a row sets or that is off its default, the value in
-- effect, where it comes from - default, catalog, catalog @<node> (that node's own
-- row) or this database (its own settings, in a users database) - its scope and
-- its default. yt_info('c:<name>') shows one setting layer by layer.
CREATE VIEW yt_config_effective AS
    SELECT x.name, j.value, j.source, j.scope, j."default" AS default_value
    FROM json_to_recordset(@extschema@.yt_info('c@' || current_database())) AS x(name text, value text, module text),
         LATERAL json_to_record(x.value::json) AS j(value text, source text, scope text, "default" text);

-- Enable or disable triggers for tables that match the patterns.
--
-- Both include and exclude patterns have LIKE-style syntax and may contain a schema name part
-- that must be separated from the table name part with a dot. Example:
--      %.nice_%    - any schema, table name starts with "nice_"
--      yt.%        - any table within "yt" schema
--      %_log       - any table name that ends with "_log"
--      yt.         - same as yt.% in the include pattern, see below
--
-- The include pattern must explicitly set at least one of the pattern parts, a completely empty
-- include pattern is not allowed, and the missing part will be considered as 'include any of'.
--
-- Same time, any empty exclude pattern parts will turn off appropriate checks. In other words,
-- if a particular table in a certain schema is flagged to be processed by include pattern, it will not
-- be flagged off with any of the empty exclude pattern parts.
--
-- If a pattern part begins with the exclamation mark symbol, it will be treated as a NOT LIKE
-- expression instead of LIKE!
CREATE FUNCTION yt_setup(
    include varchar DEFAULT '%.%',
    exclude varchar DEFAULT '',
    install bool DEFAULT true,
    cfg_table_name varchar DEFAULT NULL
)
    RETURNS TABLE (
                      schemaname TEXT,
                      tablename TEXT,
                      processed BOOLEAN
                  )
AS $$
<< main >>
    DECLARE
    -- Trigger prefix
    trigger_prefix text := 'zytsync_';
    -- Current table "path" as it appears in the yt_config table: schema.table
    cfg_name_value text;
    a text[];
    yes bool;
    row RECORD;
    r_config text;
    u_line text;
BEGIN
    -- Input check: An include pattern can't be an empty string.
    IF substring(include from '^!?[\w\s%]+\.*!?[\w\s%]*$') IS NULL THEN
        RAISE EXCEPTION 'Invalid include pattern "%"', include;
    END IF;

    IF char_length(exclude) > 0 AND substring(exclude from '^!?[\w\s%]+\.*!?[\w\s%]*$') IS NULL THEN
        RAISE EXCEPTION 'Invalid exclude pattern "%"', exclude;
    END IF;

    IF cfg_table_name IS NULL THEN
        -- This database's own config table, as ytserv holds it (yt_info('U')
        -- `cfg=`): the catalog's in the catalog, a users database's own
        -- (<extension schema>.<the catalog's table name>, or what `databases`
        -- names). A database ytserv does not replicate gets
        -- that name and a NOTICE: a registration it will not load is not silent.
        -- One ytserv does not know yet (just created, an older ytserv): the
        -- catalog's name, as before.
        select value from json_to_recordset(@extschema@.yt_info('R')) as x(name text, value text) where name = 'config'
            INTO r_config;
        select x.value from json_to_recordset(@extschema@.yt_info('U')) as x(name text, value text)
            where x.name = current_database() INTO u_line;
        IF u_line LIKE 'participant %' THEN
            cfg_table_name := substring(u_line from 'cfg=([^ ]+)');
        ELSIF u_line IS NOT NULL AND r_config IS NOT NULL THEN
            cfg_table_name := '@extschema@.' || split_part(r_config, '.', 2);
            RAISE NOTICE 'yt_setup: ytserv does not replicate database % (%) - the table config written to % is not loaded until it does, see yt_info(''U'')',
                current_database(), u_line, cfg_table_name;
        END IF;
        IF cfg_table_name IS NULL THEN
            cfg_table_name := r_config;
        END IF;
        IF cfg_table_name IS NULL THEN
            RAISE EXCEPTION 'Could not get configuration table name. Please make sure that YTServer set up and running.';
        END IF;
    END IF;

    --
    FOR row IN
        SELECT ns.nspname as schema, c.relname as table
        FROM pg_catalog.pg_class as c LEFT OUTER JOIN pg_catalog.pg_namespace as ns
                                                      ON c.relnamespace = ns.oid
        WHERE c.relkind = 'r' AND ns.nspname NOT LIKE 'pg_%' AND ns.nspname <> 'information_schema'
        LOOP
            a = regexp_split_to_array(include, E'\\.');
            yes := false;

            -- 'yes' will be true if the current schema|table matches the include pattern
            CASE array_length(a, 1)
                WHEN 1 THEN
                    IF starts_with(a[1], '!') THEN
                        yes := row.table NOT LIKE substring(a[1], 2);
                    ELSE
                        yes := row.table LIKE a[1];
                    END IF;
                WHEN 2 THEN
                    IF starts_with(a[1], '!') THEN
                        yes := row.schema NOT LIKE substring(a[1], 2);
                    ELSE
                        yes := row.schema LIKE a[1];
                    END IF;
                    IF starts_with(a[2], '!') THEN
                        yes := yes AND row.table NOT LIKE substring(a[2], 2);
                    ELSE
                        yes := yes AND row.table LIKE a[2];
                    END IF;
                END CASE;

            -- 'yes' will be true if the current schema|table doesn't match the exclude pattern
            IF yes AND char_length(exclude) > 0 THEN
                a = regexp_split_to_array(exclude, E'\\.');
                CASE array_length(a, 1)
                    WHEN 1 THEN
                        IF starts_with(a[1], '!') THEN
                            yes := NOT row.table NOT LIKE substring(a[1], 2);
                        ELSE
                            yes := NOT row.table LIKE a[1];
                        END IF;
                    WHEN 2 THEN
                        -- excluded when BOTH parts match: `public.local_%` leaves
                        -- every other table of public (an AND here excluded a table
                        -- when either part matched - all of public)
                        IF starts_with(a[1], '!') THEN
                            yes := NOT row.schema NOT LIKE substring(a[1], 2);
                        ELSE
                            yes := NOT row.schema LIKE a[1];
                        END IF;
                        IF starts_with(a[2], '!') THEN
                            yes := yes OR NOT row.table NOT LIKE substring(a[2], 2);
                        ELSE
                            yes := yes OR NOT row.table LIKE a[2];
                        END IF;
                    END CASE;
            END IF;

            IF yes THEN
                DECLARE
                    trigger_found text;
                    cfg_found text;
                    trigger_name text;
                    prev_repl_role text;
                BEGIN
                    -- `SET SESSION yt.sync = 1` used to sit here, and it did
                    -- NOTHING: no `yt.sync` GUC is defined or read anywhere in
                    -- the extension (the only custom ones are
                    -- yt_cluster.auto_complete and yt_cluster.sync_delay_ms), and
                    -- PostgreSQL silently accepts any dotted name as a custom
                    -- option. It read as the trigger-suppression this function
                    -- needs while suppressing nothing, which is worse than no
                    -- mechanism at all - see the config write below for the one
                    -- that actually works.
                    RAISE NOTICE '%.%: %', row.schema, row.table, yes;

                    -- Does the trigger already exist?
                    trigger_name := trigger_prefix || row.schema || '_' || row.table || '_trigger';
                    EXECUTE 'SELECT tgname FROM pg_trigger WHERE NOT tgisinternal AND tgname = $1'
                        INTO trigger_found
                        USING trigger_name;

                    -- Does the configuration table mention this table?
                    -- The stored name is `schema.table`. The DATABASE is added by the
                    -- server when it loads the row (ytserv does this),
                    -- so writing it here too produced `A.<db>..<db>.<schema>.<table>`
                    -- - doubled and with a stray leading dot - which matched nothing
                    -- TableRef::as_key() ever looks up. Every per-table setting
                    -- therefore silently fell back to its default.
                    cfg_name_value := row.schema || '.' || row.table;
                    EXECUTE format('SELECT name FROM %s WHERE name = $1 AND module = $2', cfg_table_name)
                        INTO cfg_found
                        USING cfg_name_value, 'A';

                    RAISE NOTICE 'trigger_found: %, cfg_found: %', trigger_found, cfg_found;

                    IF install THEN
                        RAISE NOTICE 'cfg_table_name: %', cfg_table_name;
                        a := regexp_split_to_array(cfg_table_name, E'\\.');
                        RAISE NOTICE 'schema: %', a[1];

                        -- Create a trigger
                        IF trigger_found IS NULL THEN
                            RAISE NOTICE '%', format('CREATE TRIGGER %I AFTER INSERT OR UPDATE OR DELETE ON %s FOR EACH ROW EXECUTE PROCEDURE @extschema@.yt_synchronize()', trigger_name, row.schema || '.' || row.table);
                            EXECUTE format('CREATE TRIGGER %I AFTER INSERT OR UPDATE OR DELETE ON %s FOR EACH ROW EXECUTE PROCEDURE @extschema@.yt_synchronize()'
                                , trigger_name, row.schema || '.' || row.table);
                        ELSE
                            RAISE NOTICE 'Trigger % already exists', trigger_name;
                        END IF;

                        -- TRUNCATE fires no row trigger, so without this one a
                        -- TRUNCATE emptied this node alone and the client was told
                        -- it worked. A TRUNCATE trigger can only be FOR EACH
                        -- STATEMENT; yt_synchronize sends it as DB_ACTION_TRUNCATE
                        -- (replicated as DELETE, or refused - the error rolls the
                        -- TRUNCATE back). Its own name prefix, so a long table name
                        -- truncated to 63 chars cannot collide with the row trigger;
                        -- OR REPLACE, so re-running yt_setup adds it to tables
                        -- registered before it existed. A deliberate local wipe
                        -- runs under SET LOCAL yt.node = 'local'.
                        EXECUTE format('CREATE OR REPLACE TRIGGER %I AFTER TRUNCATE ON %s FOR EACH STATEMENT EXECUTE PROCEDURE @extschema@.yt_synchronize()'
                            , 'zytsynct_' || row.schema || '_' || row.table, row.schema || '.' || row.table);

                        -- [ycoord] The table's sequences the cluster's, when its config
                        -- says so (cluster_sequence, or def_cluster_sequence): its column
                        -- defaults to yt_nextval. Not switched, the trigger stays all the same.
                        BEGIN
                            IF coalesce((SELECT x ->> 'value' FROM json_array_elements(@extschema@.yt_info('sq?:'
                                    || current_database() || '.' || row.schema || '.' || row.table)) x LIMIT 1), 'off') = 'on' THEN
                                PERFORM @extschema@.yt_cluster_sequences(format('%I.%I', row.schema, row.table)::regclass, true);
                            END IF;
                        EXCEPTION WHEN OTHERS THEN
                            RAISE WARNING 'yt_setup: %.%: its sequences stay its own: %', row.schema, row.table, SQLERRM;
                        END;

                        -- Without a single integer primary key a table is not
                        -- framed: recon compares it row by row, with no cheap whole-table
                        -- equality. Said, not refused.
                        IF NOT EXISTS (SELECT 1 FROM pg_index i
                                JOIN pg_attribute a ON a.attrelid = i.indrelid AND a.attnum = i.indkey[0]
                            WHERE i.indrelid = format('%I.%I', row.schema, row.table)::regclass
                              AND i.indisprimary AND i.indnkeyatts = 1
                              AND a.atttypid IN ('int2'::regtype, 'int4'::regtype, 'int8'::regtype)) THEN
                            RAISE NOTICE 'yt_setup: %.% has no single integer primary key - it is compared row by row, never by frames; give it one',
                                row.schema, row.table;
                        END IF;

                        -- Put a record to the configuration table.
                        --
                        -- Its OWN subtransaction, and with the config table's
                        -- own sync trigger suppressed. Both halves matter, and
                        -- between them they are the fix for a bug that made a
                        -- node look registered while being completely inert.
                        --
                        -- WHAT WENT WRONG. Once the config table is itself
                        -- registered for sync - which is the normal end state -
                        -- writing this row IS a cluster write, and on a node
                        -- that is not the leader it is refused with
                        -- "yt_synchronize: Config: This configuration update
                        -- require call from cluster leader". That error aborted
                        -- the whole statement, so the CREATE TRIGGER above
                        -- rolled back WITH it. The node ended up with neither
                        -- the trigger nor the row, while the leader's copy of
                        -- the config said the table was registered. Every write
                        -- on such a node then failed with `yt_complete: not
                        -- connected`, naming neither the table nor the reason.
                        -- Which nodes were affected depended on who won the
                        -- last election.
                        --
                        -- WHY LOCAL IS ALSO THE CORRECT SCOPE, not just the
                        -- convenient one. Registration is inherently per-node:
                        -- CREATE TRIGGER is local DDL and does not replicate.
                        -- Letting only the config row travel produced the worst
                        -- of both - peers with a config row for a table that
                        -- has no trigger, i.e. half-registered and silently so.
                        -- yt_setup is a local installation step, run on every
                        -- node (`ytctl --batch` does exactly that), so the row
                        -- belongs in this node's config table whether or not
                        -- consensus is reachable. A cluster that cannot elect
                        -- must still be installable.
                        --
                        -- set_config(..., true) is SET LOCAL, restored right
                        -- after; on any error the subtransaction rolls it back
                        -- anyway. Needs superuser or, on PG15+, GRANT SET ON
                        -- PARAMETER session_replication_role - and if it is not
                        -- permitted we say so and keep the trigger, rather than
                        -- discarding the whole registration.
                        IF cfg_found IS NULL THEN
                            BEGIN
                                prev_repl_role := current_setting('session_replication_role');
                                PERFORM set_config('session_replication_role', 'replica', true);
                                RAISE NOTICE '%', format('INSERT INTO %s(name, module, value) values ($1, $2, $3)', cfg_table_name);
                                EXECUTE format('INSERT INTO %s(name, module, value) values ($1, $2, $3)', cfg_table_name)
                                    USING cfg_name_value, 'A', '';
                                PERFORM set_config('session_replication_role', prev_repl_role, true);
                            EXCEPTION WHEN OTHERS THEN
                                -- The trigger SURVIVES this, which is the whole
                                -- point of the nested block. A warning beats a
                                -- silent half-install: the operator can add the
                                -- row by hand, and the table still syncs.
                                RAISE WARNING 'yt_setup: trigger % installed on %, but its config row was not written: % - add it with: INSERT INTO %(name, module, value) VALUES (%L, ''A'', '''')',
                                    trigger_name, cfg_name_value, SQLERRM, cfg_table_name, cfg_name_value;
                            END;
                        ELSE
                            RAISE NOTICE 'Configuration table is already mention table: "%"', cfg_name_value;
                        END IF;
                    ELSE
                        IF trigger_found IS NOT NULL THEN
                            EXECUTE format('DROP TRIGGER %s ON %s;'
                                , trigger_name, row.schema || '.' || row.table);
                        ELSE
                            RAISE NOTICE 'Trigger % is absent', trigger_name;
                        END IF;
                        EXECUTE format('DROP TRIGGER IF EXISTS %I ON %s'
                            , 'zytsynct_' || row.schema || '_' || row.table, row.schema || '.' || row.table);

                        -- Same treatment as the install path, and for the same
                        -- reason mirrored: a DELETE refused because this node is
                        -- not the leader would roll back the DROP TRIGGER above,
                        -- leaving the table still triggered but deregistered -
                        -- the half-state again, pointing the other way.
                        IF cfg_found IS NOT NULL THEN
                            BEGIN
                                prev_repl_role := current_setting('session_replication_role');
                                PERFORM set_config('session_replication_role', 'replica', true);
                                EXECUTE format('DELETE FROM %s WHERE name = $1 AND module = $2', cfg_table_name)
                                    USING cfg_name_value, 'A';
                                PERFORM set_config('session_replication_role', prev_repl_role, true);
                            EXCEPTION WHEN OTHERS THEN
                                RAISE WARNING 'yt_setup: trigger removed from %, but its config row was not deleted: % - remove it with: DELETE FROM % WHERE name = %L AND module = ''A''',
                                    cfg_name_value, SQLERRM, cfg_table_name, cfg_name_value;
                            END;
                        ELSE
                            RAISE NOTICE 'Configuration has no record for table: "%"', cfg_name_value;
                        END IF;
                    END IF;
                END;
            END IF;

            RETURN QUERY SELECT row.schema::TEXT, row.table::TEXT, yes::BOOLEAN;
        END LOOP;
END;
$$ LANGUAGE plpgsql;


-- DDL drift: a table altered in this
-- database names itself to the local ytserv, which drops its schema-gate verdict at
-- once instead of keeping it up to its TTL. ytserv not answering changes nothing - the
-- gate's per-check fingerprint (the table's column shape) catches the change too.
-- ...and ytserv reads again which sequences this database's tables' columns
-- draw from - the map by which a member's sequences are moved past a replicated row's
-- values: a table or a sequence altered (a column added as serial or identity, a
-- rename), a schema renamed, any of them dropped. With the transaction's id: until it
-- ends, what ytserv reads is the catalog from before it. A
-- temporary object's DDL says nothing. The function bodies are compared byte for
-- byte with the node's own copies, which bring an installed database up to date:
-- this script runs only at CREATE EXTENSION.
CREATE FUNCTION yt_ddl_end() RETURNS event_trigger LANGUAGE plpgsql AS $$
DECLARE r record;
BEGIN
    -- yt_ddl 3
    FOR r IN SELECT DISTINCT d.object_identity FROM pg_event_trigger_ddl_commands() d
             WHERE d.object_type = 'table' LOOP
        BEGIN
            PERFORM @extschema@.yt_info('W-:' || current_database() || '.' || r.object_identity);
        EXCEPTION WHEN OTHERS THEN NULL;
        END;
    END LOOP;
    IF EXISTS (SELECT 1 FROM pg_event_trigger_ddl_commands() d
               WHERE d.object_type IN ('table', 'sequence', 'schema', 'index')
                 AND coalesce(d.schema_name, '') NOT LIKE 'pg\_temp%') THEN
        BEGIN
            PERFORM @extschema@.yt_info('S-:' || current_database() || ':' || pg_current_xact_id());
        EXCEPTION WHEN OTHERS THEN NULL;
        END;
    END IF;
END $$;
CREATE EVENT TRIGGER yt_ddl_end ON ddl_command_end WHEN TAG IN ('ALTER TABLE')
    EXECUTE FUNCTION yt_ddl_end();
CREATE EVENT TRIGGER yt_ddl_seq ON ddl_command_end WHEN TAG IN ('ALTER SEQUENCE', 'ALTER SCHEMA')
    EXECUTE FUNCTION yt_ddl_end();
-- an index made: the unique keys a write is probed by are read again
CREATE EVENT TRIGGER yt_ddl_idx ON ddl_command_end WHEN TAG IN ('CREATE INDEX')
    EXECUTE FUNCTION yt_ddl_end();

CREATE FUNCTION yt_ddl_drop() RETURNS event_trigger LANGUAGE plpgsql AS $$
BEGIN
    -- yt_ddl 3
    IF EXISTS (SELECT 1 FROM pg_event_trigger_dropped_objects() d
               WHERE NOT d.is_temporary AND d.object_type IN ('table', 'sequence', 'schema', 'index')) THEN
        PERFORM @extschema@.yt_info('S-:' || current_database() || ':' || pg_current_xact_id());
    END IF;
EXCEPTION WHEN OTHERS THEN NULL;
END $$;
CREATE EVENT TRIGGER yt_ddl_drop ON sql_drop EXECUTE FUNCTION yt_ddl_drop();

-- Announce this database to ytserv:
-- the extension commits only after this script, so ytserv probes every second
-- for 30 s. Harmless when ytserv is not running yet.
DO $$ BEGIN PERFORM yt_info('U+'); EXCEPTION WHEN OTHERS THEN NULL; END $$;

-- Approve a pending join (a node's, or a lower cluster's CA) on the
-- leader: the leader signs the approval of the entry it holds (yt_info('J!:'))
-- and the J row records it - the yt_join_gate trigger hands the value to the
-- leader, which arms the entry. Nothing secret is written: the signature
-- verifies only under a key ytserv keeps in memory.
CREATE FUNCTION yt_approve(subject text) RETURNS text LANGUAGE plpgsql AS $$
DECLARE v text; n int;
BEGIN
    v := yt_info('J!:' || subject) -> 0 ->> 'value';
    IF v IS NULL OR v NOT LIKE 'ok:%' THEN
        RAISE EXCEPTION 'yt_approve: %', coalesce(v, 'no answer from ytserv');
    END IF;
    UPDATE @extschema@.yt_config SET value = v WHERE module = 'J' AND name = subject;
    GET DIAGNOSTICS n = ROW_COUNT;
    IF n = 0 THEN
        RAISE EXCEPTION 'yt_approve: no J row for % in @extschema@.yt_config', subject;
    END IF;
    RETURN v;
END $$;
