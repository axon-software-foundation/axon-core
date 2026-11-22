/**
 * runtime/src/graph_engine.cpp
 * ─────────────────────────────
 * Concrete implementation of IGraphEngine.
 *
 * Data structures used (all native, no Python):
 *   - Adjacency list:  std::unordered_map<AgentID, vector<GraphEdge>>
 *   - BFS/DFS:         std::queue / std::stack with visited set
 *   - Topological sort: Kahn's algorithm (iterative, no recursion limit)
 *   - Cycle detection:  DFS coloring (WHITE/GRAY/BLACK)
 */

#pragma once
#include "../interfaces/interfaces.hpp"
#include <queue>
#include <stack>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <stdexcept>

namespace axon {

/* ─── Hash support for AgentID ─────────────────────────────── */
struct AgentIDHash {
    size_t operator()(const AgentID& id) const {
        return std::hash<std::string>{}(id.value);
    }
};
struct AgentIDEq {
    bool operator()(const AgentID& a, const AgentID& b) const {
        return a.value == b.value;
    }
};

template<typename V>
using AgentMap = std::unordered_map<AgentID, V, AgentIDHash, AgentIDEq>;
using AgentSet = std::unordered_set<AgentID, AgentIDHash, AgentIDEq>;


class GraphEngine final : public IGraphEngine {
public:
    GraphEngine() = default;

    /* ── Node management ────────────────────────────────── */
    void add_node(const AgentID& id) override {
        if (!_adj.count(id)) {
            _adj[id] = {};
            _in_edges[id] = {};
        }
    }

    void add_edge(const GraphEdge& edge) override {
        add_node(edge.from);
        add_node(edge.to);
        _adj[edge.from].push_back(edge);
        _in_edges[edge.to].push_back(edge);
    }

    void remove_node(const AgentID& id) override {
        _adj.erase(id);
        _in_edges.erase(id);
        // Remove all edges pointing to/from this node
        for (auto& [node, edges] : _adj) {
            edges.erase(
                std::remove_if(edges.begin(), edges.end(),
                    [&](const GraphEdge& e){ return e.to.value == id.value; }),
                edges.end()
            );
        }
    }

    /* ── Traversal ──────────────────────────────────────── */
    std::vector<AgentID> successors(const AgentID& id) override {
        std::vector<AgentID> result;
        auto it = _adj.find(id);
        if (it == _adj.end()) return result;
        for (const auto& e : it->second)
            result.push_back(e.to);
        return result;
    }

    std::vector<AgentID> predecessors(const AgentID& id) override {
        std::vector<AgentID> result;
        auto it = _in_edges.find(id);
        if (it == _in_edges.end()) return result;
        for (const auto& e : it->second)
            result.push_back(e.from);
        return result;
    }

    /* ── BFS ─────────────────────────────────────────────── */
    std::vector<AgentID> bfs(const AgentID& start) override {
        std::vector<AgentID> order;
        AgentSet             visited;
        std::queue<AgentID>  q;

        q.push(start);
        visited.insert(start);

        while (!q.empty()) {
            AgentID cur = q.front(); q.pop();
            order.push_back(cur);

            for (const auto& e : _adj[cur]) {
                if (!visited.count(e.to)) {
                    visited.insert(e.to);
                    q.push(e.to);
                }
            }
        }
        return order;
    }

    /* ── DFS ─────────────────────────────────────────────── */
    std::vector<AgentID> dfs(const AgentID& start) override {
        std::vector<AgentID> order;
        AgentSet             visited;
        std::stack<AgentID>  stack;

        stack.push(start);

        while (!stack.empty()) {
            AgentID cur = stack.top(); stack.pop();
            if (visited.count(cur)) continue;
            visited.insert(cur);
            order.push_back(cur);

            // Push in reverse so left-most successor is processed first
            auto& edges = _adj[cur];
            for (auto it = edges.rbegin(); it != edges.rend(); ++it)
                stack.push(it->to);
        }
        return order;
    }

    /* ── Topological sort (Kahn's algorithm) ─────────────── */
    std::vector<AgentID> topological_order() override {
        if (has_cycle())
            throw std::runtime_error(
                "axon::GraphEngine: topological sort requires a DAG "
                "(cycle detected in flow graph)");

        // In-degree map
        AgentMap<int> in_degree;
        for (auto& [node, _] : _adj) in_degree[node] = 0;
        for (auto& [node, edges] : _adj)
            for (auto& e : edges)
                in_degree[e.to]++;

        std::queue<AgentID> q;
        for (auto& [node, deg] : in_degree)
            if (deg == 0) q.push(node);

        std::vector<AgentID> order;
        while (!q.empty()) {
            AgentID cur = q.front(); q.pop();
            order.push_back(cur);
            for (auto& e : _adj[cur]) {
                if (--in_degree[e.to] == 0)
                    q.push(e.to);
            }
        }
        return order;
    }

    /* ── Cycle detection (DFS 3-color) ───────────────────── */
    bool has_cycle() override {
        enum Color { WHITE, GRAY, BLACK };
        AgentMap<Color> color;
        for (auto& [node, _] : _adj) color[node] = WHITE;

        std::function<bool(const AgentID&)> visit =
            [&](const AgentID& u) -> bool {
                color[u] = GRAY;
                for (auto& e : _adj[u]) {
                    if (color[e.to] == GRAY)  return true;  // back edge
                    if (color[e.to] == WHITE && visit(e.to)) return true;
                }
                color[u] = BLACK;
                return false;
            };

        for (auto& [node, _] : _adj)
            if (color[node] == WHITE && visit(node))
                return true;
        return false;
    }

    /* ── Edge queries ─────────────────────────────────────── */
    std::vector<GraphEdge> edges_from(const AgentID& id) {
        auto it = _adj.find(id);
        return (it != _adj.end()) ? it->second : std::vector<GraphEdge>{};
    }

    // Returns edges matching a specific outcome (success/fail)
    std::vector<AgentID> next_on_success(const AgentID& id) {
        std::vector<AgentID> result;
        for (const auto& e : edges_from(id))
            if (e.on_success) result.push_back(e.to);
        return result;
    }

    std::vector<AgentID> next_on_fail(const AgentID& id) {
        std::vector<AgentID> result;
        for (const auto& e : edges_from(id))
            if (e.on_fail) result.push_back(e.to);
        return result;
    }

    /* ── Stats ───────────────────────────────────────────── */
    size_t node_count() const override { return _adj.size(); }

    size_t edge_count() const override {
        size_t n = 0;
        for (auto& [_, edges] : _adj) n += edges.size();
        return n;
    }

private:
    AgentMap<std::vector<GraphEdge>> _adj;        // outgoing edges
    AgentMap<std::vector<GraphEdge>> _in_edges;   // incoming edges
};

} // namespace axon
