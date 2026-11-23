-- SPDX-License-Identifier: MIT - Copyright (c) 2026 DBinvent
-- yt_cluster 0.5 -> 1.21: what ALTER EXTENSION yt_cluster UPDATE runs in a database that has 0.5.
-- Made by update-sql.py from yt_cluster--1.21.sql (fnv1a64 8a9b67d21a0ccc61): never edited - `make update-sql`.
--
-- It brings the database to what CREATE EXTENSION makes at 1.21, an object at a time: a function or a
-- view replaced where it can be, a table, an index or an event trigger made when it is not there. One that
-- cannot be changed in place is kept as it is and said in a WARNING - the update does not fail on it.
-- Nothing is dropped and no table is altered: one in the shape an older version gave it is said too.
-- A C function the loaded library does not have stops the update, the database left at its version:
-- that library is an older one still in memory - restart PostgreSQL and run the update again.

\echo Use "ALTER EXTENSION yt_cluster UPDATE TO '1.21'" to load this file. \quit

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_synchronize()
    RETURNS trigger
    STRICT
    SET search_path TO @extschema@
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_synchronize'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_synchronize()', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_complete(is_commit bool)
    RETURNS int
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_complete'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_complete(is_commit bool)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_info(input text)
    RETURNS json
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_info'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_info(input text)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_info()
    RETURNS json
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_info_def'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_info()', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_info(term bigint, subterm bigint)
    RETURNS json
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_info_data'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_info(term bigint, subterm bigint)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_info(term bigint, subterm bigint, version bigint)
    RETURNS json
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_info_data_version'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_info(term bigint, subterm bigint, version bigint)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_merge(term bigint, subterm bigint)
    RETURNS json
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_merge'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_merge(term bigint, subterm bigint)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_merge(term bigint, subterm bigint, force_commit bool)
    RETURNS json
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_merge_force'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_merge(term bigint, subterm bigint, force_commit bool)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_merge()
    RETURNS json
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_merge_def'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_merge()', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_s3_blob()
    RETURNS trigger
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_s3_blob'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_s3_blob()', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_s3_probe(dir text)
    RETURNS text
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_s3_probe'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_s3_probe(dir text)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
REVOKE ALL ON FUNCTION yt_s3_blob() FROM PUBLIC
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'REVOKE ALL ON FUNCTION yt_s3_blob() FROM PUBLIC', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
REVOKE ALL ON FUNCTION yt_s3_probe(text) FROM PUBLIC
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'REVOKE ALL ON FUNCTION yt_s3_probe(text) FROM PUBLIC', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_advisory_lock(key bigint)
    RETURNS void
    LANGUAGE c STRICT VOLATILE
    AS 'MODULE_PATHNAME', 'yt_advisory_lock'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_advisory_lock(key bigint)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_try_advisory_lock(key bigint)
    RETURNS boolean
    LANGUAGE c STRICT VOLATILE
    AS 'MODULE_PATHNAME', 'yt_try_advisory_lock'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_try_advisory_lock(key bigint)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_advisory_unlock(key bigint)
    RETURNS boolean
    LANGUAGE c STRICT VOLATILE
    AS 'MODULE_PATHNAME', 'yt_advisory_unlock'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_advisory_unlock(key bigint)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_advisory_xact_lock(key bigint)
    RETURNS void
    LANGUAGE c STRICT VOLATILE
    AS 'MODULE_PATHNAME', 'yt_advisory_xact_lock'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_advisory_xact_lock(key bigint)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_nextval(seq regclass)
    RETURNS bigint
    LANGUAGE c STRICT VOLATILE
    AS 'MODULE_PATHNAME', 'yt_nextval'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_nextval(seq regclass)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_create_sequence(seq regclass, cache integer DEFAULT 1, if_missing boolean DEFAULT false)
    RETURNS bigint
    LANGUAGE c VOLATILE
    AS 'MODULE_PATHNAME', 'yt_create_sequence'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_create_sequence(seq regclass, cache integer DEFAULT 1, if_missin...', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_cluster_sequences(rel regclass, cluster boolean DEFAULT true)
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
$ycoord$
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_cluster_sequences(rel regclass, cluster boolean DEFAULT true)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
REVOKE ALL ON FUNCTION yt_cluster_sequences(regclass, boolean) FROM PUBLIC
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'REVOKE ALL ON FUNCTION yt_cluster_sequences(regclass, boolean) FROM PU', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_ct_encode(input text)
    RETURNS text
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_ct_encode'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_ct_encode(input text)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_ct_decode(input text)
    RETURNS text
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_ct_decode'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_ct_decode(input text)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_diff_apply(doc jsonb, diff bytea)
    RETURNS jsonb
    STRICT
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_diff_apply'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_diff_apply(doc jsonb, diff bytea)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_diff_encode(diff jsonb)
    RETURNS bytea
    STRICT
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_diff_encode'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_diff_encode(diff jsonb)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_diff_apply_bson(doc bytea, diff bytea)
    RETURNS bytea
    STRICT
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_diff_apply_bson'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_diff_apply_bson(doc bytea, diff bytea)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_bson_to_json(doc bytea)
    RETURNS jsonb
    STRICT IMMUTABLE
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_bson_to_json'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_bson_to_json(doc bytea)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_bson_from_json(doc jsonb)
    RETURNS bytea
    STRICT IMMUTABLE
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'yt_bson_from_json'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_bson_from_json(doc jsonb)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE TABLE IF NOT EXISTS @extschema@.yt_config (
    id     serial,
    name   varchar(80)  NOT NULL,
    module char         NOT NULL DEFAULT ' ',
    value  text         NOT NULL,
    PRIMARY KEY (module, name)
)
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'table @extschema@.yt_config', SQLERRM;
END $yt_update$;

DO $yt_update$
DECLARE lacks text; keyed text;
BEGIN
    SELECT string_agg(c, ', ') INTO lacks FROM unnest(ARRAY['id', 'name', 'module', 'value']) c
     WHERE NOT EXISTS (SELECT 1 FROM pg_catalog.pg_attribute a
                        WHERE a.attrelid = '@extschema@.yt_config'::regclass AND a.attname = c AND NOT a.attisdropped);
    SELECT coalesce(string_agg(a.attname, ', ' ORDER BY k.n), '') INTO keyed
      FROM pg_catalog.pg_index i CROSS JOIN LATERAL unnest(i.indkey::int2[]) WITH ORDINALITY k(attnum, n)
      JOIN pg_catalog.pg_attribute a ON a.attrelid = i.indrelid AND a.attnum = k.attnum
     WHERE i.indrelid = '@extschema@.yt_config'::regclass AND i.indisprimary;
    IF lacks IS NOT NULL OR keyed <> 'module, name' THEN
        RAISE WARNING 'yt_cluster update to 1.21: % has the shape an older version gave it (columns it lacks: %; primary key: %, this version''s: %) - an update alters no table',
            'table @extschema@.yt_config', coalesce(lacks, 'none'), coalesce(nullif(keyed, ''), 'none'), 'module, name';
    END IF;
EXCEPTION WHEN OTHERS THEN
    RAISE WARNING 'yt_cluster update to 1.21: % is not compared with this version''s: %', 'table @extschema@.yt_config', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
PERFORM pg_catalog.pg_extension_config_dump('@extschema@.yt_config', '')
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'the dump of pg_catalog.pg_extension_config_dump(''@extschema@.yt_config'', '''')', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
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
)
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'table @extschema@.yt_stat', SQLERRM;
END $yt_update$;

DO $yt_update$
DECLARE lacks text; keyed text;
BEGIN
    SELECT string_agg(c, ', ') INTO lacks FROM unnest(ARRAY['schema', 'tabl', 'level', 'lo', 'hi', 'cnt', 'xor32', 'sum64', 'max_ts', 'computed', 'stale']) c
     WHERE NOT EXISTS (SELECT 1 FROM pg_catalog.pg_attribute a
                        WHERE a.attrelid = '@extschema@.yt_stat'::regclass AND a.attname = c AND NOT a.attisdropped);
    SELECT coalesce(string_agg(a.attname, ', ' ORDER BY k.n), '') INTO keyed
      FROM pg_catalog.pg_index i CROSS JOIN LATERAL unnest(i.indkey::int2[]) WITH ORDINALITY k(attnum, n)
      JOIN pg_catalog.pg_attribute a ON a.attrelid = i.indrelid AND a.attnum = k.attnum
     WHERE i.indrelid = '@extschema@.yt_stat'::regclass AND i.indisprimary;
    IF lacks IS NOT NULL OR keyed <> 'schema, tabl, level, lo' THEN
        RAISE WARNING 'yt_cluster update to 1.21: % has the shape an older version gave it (columns it lacks: %; primary key: %, this version''s: %) - an update alters no table',
            'table @extschema@.yt_stat', coalesce(lacks, 'none'), coalesce(nullif(keyed, ''), 'none'), 'schema, tabl, level, lo';
    END IF;
EXCEPTION WHEN OTHERS THEN
    RAISE WARNING 'yt_cluster update to 1.21: % is not compared with this version''s: %', 'table @extschema@.yt_stat', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
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
)
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'table @extschema@.yt_alert', SQLERRM;
END $yt_update$;

DO $yt_update$
DECLARE lacks text; keyed text;
BEGIN
    SELECT string_agg(c, ', ') INTO lacks FROM unnest(ARRAY['id', 'ts', 'class', 'source', 'subject', 'detail', 'pk', 'resolved']) c
     WHERE NOT EXISTS (SELECT 1 FROM pg_catalog.pg_attribute a
                        WHERE a.attrelid = '@extschema@.yt_alert'::regclass AND a.attname = c AND NOT a.attisdropped);
    SELECT coalesce(string_agg(a.attname, ', ' ORDER BY k.n), '') INTO keyed
      FROM pg_catalog.pg_index i CROSS JOIN LATERAL unnest(i.indkey::int2[]) WITH ORDINALITY k(attnum, n)
      JOIN pg_catalog.pg_attribute a ON a.attrelid = i.indrelid AND a.attnum = k.attnum
     WHERE i.indrelid = '@extschema@.yt_alert'::regclass AND i.indisprimary;
    IF lacks IS NOT NULL OR keyed <> 'id' THEN
        RAISE WARNING 'yt_cluster update to 1.21: % has the shape an older version gave it (columns it lacks: %; primary key: %, this version''s: %) - an update alters no table',
            'table @extschema@.yt_alert', coalesce(lacks, 'none'), coalesce(nullif(keyed, ''), 'none'), 'id';
    END IF;
EXCEPTION WHEN OTHERS THEN
    RAISE WARNING 'yt_cluster update to 1.21: % is not compared with this version''s: %', 'table @extschema@.yt_alert', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE INDEX IF NOT EXISTS yt_alert_open_idx
    ON @extschema@.yt_alert (source, subject) WHERE resolved IS NULL
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'index yt_alert_open_idx', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
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
)
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'table @extschema@.yt_job', SQLERRM;
END $yt_update$;

DO $yt_update$
DECLARE lacks text; keyed text;
BEGIN
    SELECT string_agg(c, ', ') INTO lacks FROM unnest(ARRAY['id', 'kind', 'tabl', 'scope', 'params', 'state', 'reason', 'requested_by', 'approved_by', 'created', 'updated', 'history']) c
     WHERE NOT EXISTS (SELECT 1 FROM pg_catalog.pg_attribute a
                        WHERE a.attrelid = '@extschema@.yt_job'::regclass AND a.attname = c AND NOT a.attisdropped);
    SELECT coalesce(string_agg(a.attname, ', ' ORDER BY k.n), '') INTO keyed
      FROM pg_catalog.pg_index i CROSS JOIN LATERAL unnest(i.indkey::int2[]) WITH ORDINALITY k(attnum, n)
      JOIN pg_catalog.pg_attribute a ON a.attrelid = i.indrelid AND a.attnum = k.attnum
     WHERE i.indrelid = '@extschema@.yt_job'::regclass AND i.indisprimary;
    IF lacks IS NOT NULL OR keyed <> 'id' THEN
        RAISE WARNING 'yt_cluster update to 1.21: % has the shape an older version gave it (columns it lacks: %; primary key: %, this version''s: %) - an update alters no table',
            'table @extschema@.yt_job', coalesce(lacks, 'none'), coalesce(nullif(keyed, ''), 'none'), 'id';
    END IF;
EXCEPTION WHEN OTHERS THEN
    RAISE WARNING 'yt_cluster update to 1.21: % is not compared with this version''s: %', 'table @extschema@.yt_job', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
PERFORM pg_catalog.pg_extension_config_dump('@extschema@.yt_job', '')
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'the dump of pg_catalog.pg_extension_config_dump(''@extschema@.yt_job'', '''')', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION @extschema@.yt_llm_query(nl text, page int DEFAULT NULL, size int DEFAULT NULL)
    RETURNS json
    LANGUAGE c VOLATILE
    AS 'MODULE_PATHNAME', 'yt_llm_query'
;
EXCEPTION
    WHEN undefined_function OR undefined_file THEN
        RAISE EXCEPTION 'yt_cluster update to 1.21: the yt_cluster library PostgreSQL has loaded is not this version''s (%): restart PostgreSQL, then run ALTER EXTENSION yt_cluster UPDATE again', SQLERRM;
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function @extschema@.yt_llm_query(nl text, page int DEFAULT NULL, size int D...', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
REVOKE ALL ON FUNCTION @extschema@.yt_llm_query(text, int, int) FROM PUBLIC
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'REVOKE ALL ON FUNCTION @extschema@.yt_llm_query(text, int, int) FROM P', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
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
$$
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function @extschema@.yt_llm(nl text)', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE VIEW yt_tables_cfg as select name, yt_ct_decode(value) from @extschema@.yt_config where module='A'
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'view yt_tables_cfg', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE VIEW yt_help_with_defaults AS
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
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'view yt_help_with_defaults', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE VIEW yt_config_effective AS
    SELECT x.name, j.value, j.source, j.scope, j."default" AS default_value
    FROM json_to_recordset(@extschema@.yt_info('c@' || current_database())) AS x(name text, value text, module text),
         LATERAL json_to_record(x.value::json) AS j(value text, source text, scope text, "default" text)
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'view yt_config_effective', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_setup(
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
$$ LANGUAGE plpgsql
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_setup( include varchar DEFAULT ''%.%'', exclude varchar DEFAULT ''''...', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_ddl_end() RETURNS event_trigger LANGUAGE plpgsql AS $$
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
END $$
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_ddl_end()', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
IF NOT EXISTS (SELECT 1 FROM pg_catalog.pg_event_trigger WHERE evtname = 'yt_ddl_end') THEN
CREATE EVENT TRIGGER yt_ddl_end ON ddl_command_end WHEN TAG IN ('ALTER TABLE')
    EXECUTE FUNCTION yt_ddl_end()
;
END IF
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'event trigger yt_ddl_end', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
IF NOT EXISTS (SELECT 1 FROM pg_catalog.pg_event_trigger WHERE evtname = 'yt_ddl_seq') THEN
CREATE EVENT TRIGGER yt_ddl_seq ON ddl_command_end WHEN TAG IN ('ALTER SEQUENCE', 'ALTER SCHEMA')
    EXECUTE FUNCTION yt_ddl_end()
;
END IF
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'event trigger yt_ddl_seq', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
IF NOT EXISTS (SELECT 1 FROM pg_catalog.pg_event_trigger WHERE evtname = 'yt_ddl_idx') THEN
CREATE EVENT TRIGGER yt_ddl_idx ON ddl_command_end WHEN TAG IN ('CREATE INDEX')
    EXECUTE FUNCTION yt_ddl_end()
;
END IF
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'event trigger yt_ddl_idx', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_ddl_drop() RETURNS event_trigger LANGUAGE plpgsql AS $$
BEGIN
    -- yt_ddl 3
    IF EXISTS (SELECT 1 FROM pg_event_trigger_dropped_objects() d
               WHERE NOT d.is_temporary AND d.object_type IN ('table', 'sequence', 'schema', 'index')) THEN
        PERFORM @extschema@.yt_info('S-:' || current_database() || ':' || pg_current_xact_id());
    END IF;
EXCEPTION WHEN OTHERS THEN NULL;
END $$
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_ddl_drop()', SQLERRM;
END $yt_update$;

DO $yt_update$ BEGIN
IF NOT EXISTS (SELECT 1 FROM pg_catalog.pg_event_trigger WHERE evtname = 'yt_ddl_drop') THEN
CREATE EVENT TRIGGER yt_ddl_drop ON sql_drop EXECUTE FUNCTION yt_ddl_drop()
;
END IF
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'event trigger yt_ddl_drop', SQLERRM;
END $yt_update$;

DO $$ BEGIN PERFORM yt_info('U+'); EXCEPTION WHEN OTHERS THEN NULL; END $$
;

DO $yt_update$ BEGIN
CREATE OR REPLACE FUNCTION yt_approve(subject text) RETURNS text LANGUAGE plpgsql AS $$
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
END $$
;
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'yt_cluster update to 1.21: % is kept as it is: %', 'function yt_approve(subject text)', SQLERRM;
END $yt_update$;

