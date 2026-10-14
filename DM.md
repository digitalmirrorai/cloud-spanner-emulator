# Digital Mirror's emulator

This branch is upstream `v1.5.58` plus the commits below, published as
`ghcr.io/digitalmirrorai/cloud-spanner-emulator:<upstream version>-dm.<n>`.

**The clock follows the system clock** (`common/clock.cc`). Upstream's clock steps a microsecond
ahead of the system clock on every call made within a microsecond of the last, and never comes
back; a strong read waits for the system clock to reach its timestamp, so every read and query
waits the whole lead. A container that has served a day's requests answers a key read in tens of
milliseconds instead of one. Upstream accepted this change in issue #277 in October 2025 and has not
released it.

**A query reads only the keys its filters allow** (`backend/query/queryable_table.cc`). Upstream
reads every row of every table a query names and lets the evaluator filter. The evaluator offers its
equality and `IN` filters before the first row; this branch turns those on the leading primary key
columns into a key set, so a query filtered on `tenantId` reads one tenant's rows. Set
`SPANNER_EMULATOR_DISABLE_KEY_FILTER_PUSHDOWN=1` on the container to read whole tables as before.
An `UPDATE` or `DELETE` is narrowed the same way (`build/bazel/googlesql.patch`, in the algebrizer):
upstream GoogleSQL algebrizes the modified table as a bare scan, so every row was read; now the
equality and `IN` conjuncts of the `WHERE` clause reach that scan as redundant filters, and the
statement still evaluates its whole `WHERE` on every row it gets. A keyed `UPDATE` on a 4,000-row
table: 19 ms to 1.5 ms.

**A statement no longer pays for the schema** (`backend/query/catalog.cc`,
`backend/query/queryable_table.cc`, `backend/query/column_expression_analysis_cache.*`,
`backend/query/queryable_property_graph.cc`). Upstream builds a catalog for every statement and,
doing so, deep-copied the analyzer options once per table and once per column, analyzed every
default and generated column expression, and wrapped every property graph with all its property
definitions: 25 ms per statement on a schema of 195 tables, 59 expression columns and one graph,
before the first row was read. Now the options are passed by reference, each column analysis is
kept per query engine (owned by its function catalog, since the resolved tree points at that
catalog's functions; expressions over sequences, SQL UDFs or subqueries are never kept), and a
graph is wrapped on the first statement that names it. `SELECT 1` on that schema: 13 ms to 1.1 ms.
Since dm.4 a table is wrapped on the first statement that names it too: wrapping all 201 tables and
formatting every column's type name was half of what `SELECT 1` still paid. 0.8 ms to 0.4 ms through
the Java client. A column analysis is never kept when its expression holds a sequence at any depth,
as the `GET_NEXT_SEQUENCE_VALUE` of an identity column does: dm.2 and dm.3 looked only above the
first function call, kept such an analysis across catalogs, and crashed on the next use.

**A streaming query is analyzed once, not twice** (`backend/query/query_engine.cc`). Upstream's
`ExecuteStreamingSql` handler — the one every client library uses for queries — analyzes each
statement a first time, against a catalog built from nothing, only to ask whether it reads a change
stream, then hands it to `ExecuteSql` to be analyzed again. A change stream query names a
`READ_<stream>` function of a stream the schema holds, so the first pass is skipped when the schema
has no change streams or the statement mentions no `read_` at all. `SELECT 1` through the Java
client: 5.4 ms to 0.9 ms, what the non-streaming path already cost.

**A schema change is linear in the schema** (`backend/schema/graph/schema_graph_editor.*`). Cloning the
schema graph for a DDL statement asked, for every node reference it fixed up, whether the node was
original by scanning the whole graph; a set lookup now. Creating a database from 394 statements:
7 s to 1.5 s.

**A read whose timestamp has passed does not sleep** (`backend/locking/manager.cc`). A timed wait on
a deadline already behind still costs the kernel's timer slack, 50 µs, and a strong read asked five
times per statement.

**A key column is compared once** (`backend/datamodel/key.cc`), with one three-way compare for
strings, bytes, integers and booleans instead of `LessThan` and then `Equals`: a 1,000-row commit
174 ms to 132 ms.

**A modifying or graph statement is analyzed once** (`backend/query/query_engine.cc`): both need an
analysis that keeps every column, which is known from the statement text before the first pass.

**Phase timers** (`common/phase_timer.*`): with `SPANNER_EMULATOR_PHASE_TIMERS=1` on the container,
the server prints per-phase wall time and per-table rows read to stderr after each quiet second.
Off, each timer is one branch.

## Building

`docker build . -f build/docker/Dockerfile.ubuntu -t cloud-spanner-emulator:dev` — a clean build
compiles GoogleSQL and takes a couple of hours. Images are built and pushed by hand; no workflow
runs in this repository. To iterate, keep the build stage
(`docker build --target build -t emulator-build .`), copy changed sources into a container from it
and run `bazel build -c opt //binaries:emulator_main //binaries:gateway_main` there; a rebuild after
a source change takes minutes.

## Taking a new upstream release

Rebase the commits onto the new tag, rerun `bazel test //common:clock_test
//backend/query:queryable_table_test //backend/query:catalog_test //backend/query:query_engine_test
//backend/datamodel:key_test //backend/schema/updater:schema_updater_test` and the conformance target
`//tests/conformance/endpoints:emulator_conformance_test`, then tag `v<version>-dm.1`.
