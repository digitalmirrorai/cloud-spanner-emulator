# Digital Mirror's emulator

This branch is upstream `v1.5.58` plus four commits, published as
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
`UPDATE` and `DELETE` statements are not narrowed: the evaluator does not offer their `WHERE` to the
scan.

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

**A streaming query is analyzed once, not twice** (`backend/query/query_engine.cc`). Upstream's
`ExecuteStreamingSql` handler — the one every client library uses for queries — analyzes each
statement a first time, against a catalog built from nothing, only to ask whether it reads a change
stream, then hands it to `ExecuteSql` to be analyzed again. A change stream query names a
`READ_<stream>` function of a stream the schema holds, so the first pass is skipped when the schema
has no change streams or the statement mentions no `read_` at all. `SELECT 1` through the Java
client: 5.4 ms to 0.9 ms, what the non-streaming path already cost.

## Building

`docker build . -f build/docker/Dockerfile.ubuntu -t cloud-spanner-emulator:dev` — a clean build
compiles GoogleSQL and takes a couple of hours. Images are built and pushed by hand; no workflow
runs in this repository. To iterate, keep the build stage
(`docker build --target build -t emulator-build .`), copy changed sources into a container from it
and run `bazel build -c opt //binaries:emulator_main //binaries:gateway_main` there; a rebuild after
a source change takes minutes.

## Taking a new upstream release

Rebase the three commits onto the new tag, rerun `bazel test //common:clock_test
//backend/query:queryable_table_test //backend/query:catalog_test //backend/query:query_engine_test`, then tag `v<version>-dm.1`.
