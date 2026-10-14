//
// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "backend/query/column_expression_analysis_cache.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/function.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {

namespace {

// The group QueryableUdf registers SQL UDFs under (kSqlUdfGroup in
// queryable_udf.h, which this target cannot depend on without a cycle through
// index_hint_validator).
constexpr absl::string_view kSqlUdfFunctionGroup = "SQL_UDF";

// Finds, at any depth, a node that ties an analysis to the catalog it was made
// with: a sequence, a subquery or a call of a SQL UDF. A sequence is most often
// an argument of a function call, as in GET_NEXT_SEQUENCE_VALUE(SEQUENCE s) of
// an identity column, so the search must not stop at function calls.
class UnshareableNodeFinder : public googlesql::ResolvedASTVisitor {
 public:
  bool found() const { return found_; }

 private:
  absl::Status VisitResolvedSequence(
      const googlesql::ResolvedSequence* node) override {
    found_ = true;
    return absl::OkStatus();
  }
  absl::Status VisitResolvedSubqueryExpr(
      const googlesql::ResolvedSubqueryExpr* node) override {
    found_ = true;
    return absl::OkStatus();
  }
  absl::Status VisitResolvedFunctionCall(
      const googlesql::ResolvedFunctionCall* node) override {
    if (node->function()->GetGroup() == kSqlUdfFunctionGroup) {
      found_ = true;
      return absl::OkStatus();
    }
    return DefaultVisit(node);
  }

  bool found_ = false;
};

}  // namespace

std::string ColumnExpressionAnalysisCache::KeyFor(
    const Column* column, const googlesql::AnalyzerOptions& options) {
  std::string key = absl::StrCat(
      reinterpret_cast<uintptr_t>(column), "/", column->FullName(), "/",
      column->expression().value(), "/", options.default_time_zone().name());
  for (const Column* dep : column->dependent_columns()) {
    absl::StrAppend(&key, "/", dep->Name(), ":", dep->GetType()->DebugString());
  }
  return key;
}

std::shared_ptr<const googlesql::AnalyzerOutput>
ColumnExpressionAnalysisCache::Find(const std::string& key) {
  absl::MutexLock lock(&mu_);
  auto it = entries_.find(key);
  return it == entries_.end() ? nullptr : it->second;
}

void ColumnExpressionAnalysisCache::Store(
    const std::string& key,
    std::shared_ptr<const googlesql::AnalyzerOutput> output) {
  if (!IsShareable(*output)) {
    return;
  }
  absl::MutexLock lock(&mu_);
  if (entries_.size() >= kMaxEntries) {
    entries_.clear();
  }
  entries_[key] = std::move(output);
}

bool ColumnExpressionAnalysisCache::IsShareable(
    const googlesql::AnalyzerOutput& output) {
  UnshareableNodeFinder finder;
  if (!output.resolved_expr()->Accept(&finder).ok()) {
    return false;
  }
  return !finder.found();
}

}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google
