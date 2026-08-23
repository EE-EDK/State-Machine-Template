"""Graph analytics on the function-level call graph (stdlib only).

All algorithms are deterministic (sorted iteration order, fixed tie-breaks)
so the report diffs cleanly between runs.

  betweenness      Brandes, directed
  pagerank         power iteration, directed
  scc              Tarjan, iterative (cycles in the call graph)
  articulation     Tarjan, undirected (single points of failure)
  label_propagation  structural communities, undirected, deterministic
  modularity       Newman Q for a partition (undirected, unit weights)
  layers           longest path to a sink on the SCC condensation
  bowtie           IN / waist / OUT decomposition around a chosen waist
"""

from __future__ import annotations

from collections import defaultdict, deque

Adj = dict[str, set[str]]


def undirected(adj: Adj) -> Adj:
    u: Adj = {n: set() for n in adj}
    for a, nbrs in adj.items():
        for b in nbrs:
            u.setdefault(a, set()).add(b)
            u.setdefault(b, set()).add(a)
    return u


def reverse(adj: Adj) -> Adj:
    r: Adj = {n: set() for n in adj}
    for a, nbrs in adj.items():
        for b in nbrs:
            r.setdefault(b, set()).add(a)
    return r


def betweenness(adj: Adj) -> dict[str, float]:
    nodes = sorted(adj)
    cb = {v: 0.0 for v in nodes}
    for s in nodes:
        stack: list[str] = []
        pred: dict[str, list[str]] = {v: [] for v in nodes}
        sigma = {v: 0.0 for v in nodes}
        sigma[s] = 1.0
        dist = {v: -1 for v in nodes}
        dist[s] = 0
        q = deque([s])
        while q:
            v = q.popleft()
            stack.append(v)
            for w in sorted(adj.get(v, ())):
                if dist[w] < 0:
                    dist[w] = dist[v] + 1
                    q.append(w)
                if dist[w] == dist[v] + 1:
                    sigma[w] += sigma[v]
                    pred[w].append(v)
        delta = {v: 0.0 for v in nodes}
        while stack:
            w = stack.pop()
            for v in pred[w]:
                delta[v] += (sigma[v] / sigma[w]) * (1.0 + delta[w])
            if w != s:
                cb[w] += delta[w]
    return cb


def pagerank(adj: Adj, damping: float = 0.85, iters: int = 100) -> dict[str, float]:
    nodes = sorted(adj)
    n = len(nodes)
    if n == 0:
        return {}
    rank = {v: 1.0 / n for v in nodes}
    out_deg = {v: len(adj[v]) for v in nodes}
    rev = reverse(adj)
    for _ in range(iters):
        dangling = sum(rank[v] for v in nodes if out_deg[v] == 0)
        new = {}
        for v in nodes:
            s = sum(rank[u] / out_deg[u] for u in rev.get(v, ()) if out_deg[u])
            new[v] = (1 - damping) / n + damping * (s + dangling / n)
        rank = new
    return rank


def scc(adj: Adj) -> list[list[str]]:
    """Strongly connected components (Tarjan, iterative)."""
    index = {}
    low = {}
    on_stack = set()
    stack: list[str] = []
    comps: list[list[str]] = []
    counter = 0
    for root in sorted(adj):
        if root in index:
            continue
        work = [(root, iter(sorted(adj[root])))]
        index[root] = low[root] = counter
        counter += 1
        stack.append(root)
        on_stack.add(root)
        while work:
            v, it = work[-1]
            advanced = False
            for w in it:
                if w not in index:
                    index[w] = low[w] = counter
                    counter += 1
                    stack.append(w)
                    on_stack.add(w)
                    work.append((w, iter(sorted(adj.get(w, ())))))
                    advanced = True
                    break
                elif w in on_stack:
                    low[v] = min(low[v], index[w])
            if advanced:
                continue
            work.pop()
            if work:
                low[work[-1][0]] = min(low[work[-1][0]], low[v])
            if low[v] == index[v]:
                comp = []
                while True:
                    w = stack.pop()
                    on_stack.discard(w)
                    comp.append(w)
                    if w == v:
                        break
                comps.append(sorted(comp))
    return sorted(comps, key=lambda c: (-len(c), c[0]))


def articulation_points(uadj: Adj) -> set[str]:
    disc: dict[str, int] = {}
    low: dict[str, int] = {}
    parent: dict[str, str | None] = {}
    ap: set[str] = set()
    t = 0
    for root in sorted(uadj):
        if root in disc:
            continue
        disc[root] = low[root] = t
        t += 1
        parent[root] = None
        children = 0
        work = [(root, iter(sorted(uadj[root])))]
        while work:
            v, it = work[-1]
            pushed = False
            for w in it:
                if w not in disc:
                    parent[w] = v
                    disc[w] = low[w] = t
                    t += 1
                    if v == root:
                        children += 1
                    work.append((w, iter(sorted(uadj[w]))))
                    pushed = True
                    break
                elif w != parent[v]:
                    low[v] = min(low[v], disc[w])
            if pushed:
                continue
            work.pop()
            if work:
                p = work[-1][0]
                low[p] = min(low[p], low[v])
                if parent[p] is not None and low[v] >= disc[p]:
                    ap.add(p)
        if children > 1:
            ap.add(root)
    return ap


def label_propagation(uadj: Adj, max_iter: int = 50) -> dict[str, int]:
    nodes = sorted(uadj)
    label = {v: i for i, v in enumerate(nodes)}
    for _ in range(max_iter):
        changed = False
        for v in nodes:
            if not uadj[v]:
                continue
            counts: dict[int, int] = defaultdict(int)
            for w in uadj[v]:
                counts[label[w]] += 1
            best = min(counts.items(), key=lambda kv: (-kv[1], kv[0]))[0]
            if best != label[v]:
                label[v] = best
                changed = True
        if not changed:
            break
    # renumber by size
    groups: dict[int, list[str]] = defaultdict(list)
    for v, l in label.items():
        groups[l].append(v)
    order = sorted(groups.values(), key=lambda g: (-len(g), g[0]))
    return {v: i for i, grp in enumerate(order) for v in grp}


def modularity(uadj: Adj, partition: dict[str, object]) -> float:
    m = sum(len(n) for n in uadj.values()) / 2.0
    if m == 0:
        return 0.0
    deg = {v: len(uadj[v]) for v in uadj}
    q = 0.0
    for v in uadj:
        for w in uadj[v]:
            if partition.get(v) == partition.get(w):
                q += 1.0 - deg[v] * deg[w] / (2.0 * m)
    # self-pairs with no edge
    by_part: dict[object, list[str]] = defaultdict(list)
    for v, p in partition.items():
        by_part[p].append(v)
    for members in by_part.values():
        for v in members:
            for w in members:
                if w not in uadj[v] and v != w:
                    q -= deg[v] * deg[w] / (2.0 * m)
    return q / (2.0 * m)


def layers(adj: Adj) -> dict[str, int]:
    """Longest path to a sink, computed on the SCC condensation so cycles
    collapse to one layer. Sinks (no callees) are layer 0."""
    comps = scc(adj)
    comp_of = {v: i for i, c in enumerate(comps) for v in c}
    cadj: dict[int, set[int]] = {i: set() for i in range(len(comps))}
    for v, nbrs in adj.items():
        for w in nbrs:
            if comp_of[v] != comp_of[w]:
                cadj[comp_of[v]].add(comp_of[w])
    memo: dict[int, int] = {}

    def depth(c: int) -> int:
        if c in memo:
            return memo[c]
        memo[c] = 0
        d = 0
        for n in cadj[c]:
            d = max(d, depth(n) + 1)
        memo[c] = d
        return d

    for c in range(len(comps)):
        depth(c)
    return {v: memo[comp_of[v]] for v in adj}


def reachable(adj: Adj, sources: set[str]) -> set[str]:
    seen = set(sources)
    q = deque(sources)
    while q:
        v = q.popleft()
        for w in adj.get(v, ()):
            if w not in seen:
                seen.add(w)
                q.append(w)
    return seen


def bowtie(adj: Adj, waist: set[str]) -> dict[str, set[str]]:
    rev = reverse(adj)
    out_set = reachable(adj, waist) - waist
    in_set = reachable(rev, waist) - waist
    tubes = in_set & out_set
    others = set(adj) - waist - in_set - out_set
    return {"in": in_set - tubes, "waist": waist, "out": out_set - tubes,
            "tubes": tubes, "disconnected": others}


def clustering(uadj: Adj) -> float:
    total = 0.0
    n = 0
    for v, nbrs in uadj.items():
        k = len(nbrs)
        if k < 2:
            continue
        links = 0
        nl = sorted(nbrs)
        for i, a in enumerate(nl):
            for b in nl[i + 1:]:
                if b in uadj[a]:
                    links += 1
        total += 2.0 * links / (k * (k - 1))
        n += 1
    return total / n if n else 0.0
