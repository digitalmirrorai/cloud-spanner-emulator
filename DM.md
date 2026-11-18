# Digital Mirror's emulator

This branch is upstream `v1.5.59` plus the commits below, published as
`ghcr.io/digitalmirrorai/cloud-spanner-emulator:<upstream version>-dm.<n>`.

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

**A WITH entry used inside a subquery is evaluated once** (`build/bazel/googlesql.patch`, in the
algebrizer). GoogleSQL's evaluator inlines a WITH entry referenced exactly once at its use site, and
a subquery expression is evaluated again for every row of the query around it, so an entry
referenced from `IN (SELECT … FROM entry)` was evaluated once per outer row — and in a chain of such
entries, each referenced from the subquery of the next, the innermost once per row of every level
above it: four levels over 60 contracts read `ContractFact` 216,000 times, 212 s. A reference from
inside a subquery expression now counts as two, so the entry takes the path an entry referenced
twice always took: evaluated once at the start of the statement, read from an array after that. A
WITH clause written inside a subquery inlines none of its entries. An entry referenced once from a
`FROM` clause is still inlined. Since every non-inlined entry is evaluated before the first row, an
entry now evaluated once runs even where its subquery would never have been reached, as upstream's
own default (no inlining at all) does: an error in it, such as a division by zero, is raised. That
query over 45 contracts: 63 s to 20 ms; over 60, 212 s to 17 ms and 13 reads of `ContractFact`.

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

**A read whose timestamp has passed does not sleep** (`backend/locking/manager.cc`). A timed wait on
a deadline already behind still costs the kernel's timer slack, 50 µs, and a strong read asked five
times per statement.

**A key column is compared once** (`backend/datamodel/key.cc`), with one three-way compare for
strings, bytes, integers and booleans instead of `LessThan` and then `Equals`: a 1,000-row commit
174 ms to 132 ms.

**A modifying or graph statement is analyzed once** (`backend/query/query_engine.cc`): both need an
analysis that keeps every column, which is known from the statement text before the first pass.

**A database created from statements already built copies their schema**
(`backend/database/schema_cache.*`, `backend/database/database.cc`). A test run starts several JVMs
that each create a database from the same statements on one emulator, at the same moment, and every
create applied all of them: each statement clones and validates the whole schema graph, 2.3 s of CPU
for 506 statements. Now the first create builds and publishes a copy of its schema; a create that
arrives meanwhile waits for it, and each later one clones the published graph once, into nodes of
its own, and moves its table and column ID generators to where the build left them. The second
database of that schema: 2.3 s to 10 ms; three at once: 9 s of CPU to 2.3 s. The copies share only
their column types, so the builder's type factory lives as long as the cache or a database that
copied holds it. A schema is published only if its build left nothing of the one database: no
sequence (an identity column makes one), change stream or queue in it, and nothing written to or
dropped from storage; PostgreSQL databases always build their own. The last four distinct statement
lists are kept, keyed by dialect, feature flags, proto descriptors and every statement byte for
byte. Set `SPANNER_EMULATOR_DISABLE_SCHEMA_CACHE=1` on the container to build every database.

**A schema change does not keep a snapshot per statement**
(`backend/schema/updater/schema_updater.cc`). Upstream keeps the schema snapshot of every statement
of a change until the change ends, because the backfills and verifiers that run at the end read
them, and a snapshot is a copy of every node: 800 MB for the 506 statements above, three creates at
once 2.8 GB, most of it never returned. A snapshot is now released as soon as neither its own
statement nor the next has a backfill or verifier left to run. A create runs them right after each
statement, since an empty database has no rows a later statement could still invalidate, and so
holds two snapshots at most; an update still applies every statement first and keeps the snapshots
its backfills read. The emulator's memory after that create: 800 MB to 64 MB.

**Old versions and deleted rows are removed** (`backend/storage/in_memory_storage.*`,
`backend/schema/catalog/versioned_catalog.*`, `backend/transaction/read_only_transaction.cc`).
Upstream keeps every row it ever held: a delete writes a marker into every cell of the row, so
deleting adds memory, and a cell's old versions are trimmed only when that cell is written again,
which a deleted row never is. A test suite that deletes its tenant's rows after every class grew the
emulator by the size of every row it ever wrote. Now every write that gives a cell a second version,
and every delete, queues its row; once the write is older than the version retention period the row
is collected: in each cell everything older than the newest version at or before the horizon goes,
and a row whose newest such version is its delete goes entirely. Every read the retention period
allows sees what it saw before. Each write, delete and read-only read collects at most 256 due rows,
or twice what it queued, so no call holds the storage lock long on the collector's account. The
retention period is the database's `version_retention_period`, one hour unless set; set
`SPANNER_EMULATOR_VERSION_RETENTION_S` on the container (whole seconds) to keep versions for at
most that long in every database, as a test emulator can: a stale read, or a read-only transaction,
older than it fails with `FAILED_PRECONDITION` as one older than the database's own period always
did. A read is also refused below the newest horizon anything was collected at, which a shorter
period that grew again leaves behind (upstream returned what was left). Ten cycles of writing and
deleting 20,000 rows of seven columns in one database: 861 MB resident before, 210 MB with the
variable at 5 s, flat from the third cycle on. Without it nothing is collected within the hour, as
before.

**A row is a sorted array of numbered columns** (`backend/storage/in_memory_storage.*`). Upstream
held each row as a hash map from column ID string to a `std::map` of versions: a hash table sized
for growth, a copy of every column ID, and a tree node per version, about 80 bytes of table slot and
64 of node for each cell before its value. A row is now the versions of its existence and a vector of
columns sorted by a number the storage gives each column ID the first time it sees it, each column's
versions in an inlined vector that holds the usual single version without an allocation: 48 bytes a
cell. A delete adds a version to the row's existence only, not a marker to every cell; a read sees a
cell's version only if it is no older than the row's last creation, which reads exactly as the
markers did (a value written earlier in the commit that deletes the row is dropped with it). A read
looks up each column's number once per call, not once per row. Writing 20,000 rows of seven columns
(600 bytes of strings each) added 70 MB of resident memory before and 38 MB now, 3.5 KB a row to
1.9 KB.

**Phase timers** (`common/phase_timer.*`): with `SPANNER_EMULATOR_PHASE_TIMERS=1` on the container,
the server prints per-phase wall time and per-table rows read to stderr after each quiet second.
Off, each timer is one branch.

## Taken upstream

Two of this branch's patches are in upstream v1.5.59, so the branch no longer carries them: **the
clock follows the system clock** (`common/clock.cc`, upstream issue #389), and **a schema change is
linear in the schema** (`backend/schema/graph/schema_graph_editor.*`, a set lookup instead of a scan
of the graph for every node reference a clone fixes up).

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
