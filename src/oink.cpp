/*
 * Copyright 2017-2018 Tom van Dijk, Johannes Kepler University Linz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <cassert>
#include <queue>
#include <stack>
#include <iostream>

#include "oink.hpp"
#include "solvers.hpp"
#include "solver.hpp"
#include "lace.h"

namespace pg {

Oink::Oink(Game &game, std::ostream &out) : game(&game), logger(out), todo(game.n_nodes), disabled(game.n_nodes)
{
    // initialize outcount (for flush/attract)
    outcount = new int[game.n_nodes];
    for (int i=0; i<game.n_nodes; i++) {
        outcount[i] = std::count_if(game.out[i].begin(), game.out[i].end(),
                [&] (const int n) { return disabled[n] == 0; });
    }
}

Oink::~Oink()
{
    delete[] outcount;
}

/**
 * Find all SCCs at priority p, game limited to nodes with priority <= p 
 * Then for every SCC that contains edges and a node with priority p, can win
 * with priority p and is therefore a counterexample to the strategy.
 */
int
Oink::solveTrivialCycles()
{
    // Record number of trivial cycles
    int count = 0;

    // Allocate and initialize datastructures
    const int n_nodes = game->n_nodes;
    int *done = new int[n_nodes];
    int64_t *low = new int64_t[n_nodes];
    for (int i=0; i<n_nodes; i++) done[i] = disabled[i] ? -2 : -1;
    for (int i=0; i<n_nodes; i++) low[i] = 0;

    std::vector<int> res;
    std::vector<int> scc;
    std::stack<int> st;
    std::queue<int> q;

    const auto &in = game->in;
    const auto &out = game->out;
    const auto &owner = game->owner;
    const auto &priority = game->priority;

    int64_t pre = 0;

    for (int i=n_nodes-1; i>=0; i--) {
        if (disabled[i]) continue;
        if (done[i] == -2) continue; // already know to skip this

        /**
         * We're going to search all winner-controlled SCCs reachable from node <i>
         */
        const int pr = game->priority[i];
        const int pl = pr & 1;

        /**
         * Only start at unsolved winner-controlled and not yet seen for this priority
         */
        if (owner[i] != pl) {
            done[i] = -2; // highest priority but not winner-controlled, never check again
            continue;
        }

        if (done[i] == pr) continue; // already visited in this search

        /**
         * Set bot to pre...
         */
        int64_t bot = pre;

        /**
         * Start forward DFS at <i>
         */
        st.push(i);
        while (!st.empty()) {
            int idx = st.top();

            /**
             * When we see it for the first time, we assign the next number to it and add it to <res>.
             */
            if (low[idx] <= bot) {
                low[idx] = ++pre;
                if (pre < 0) LOGIC_ERROR; // overflow on a 64-bit integer...
                res.push_back(idx);
            }

            /**
             * Now we check all outgoing (allowed) edges.
             * If seen earlier, then update "min"
             * If new, then 'recurse'
             */
            int min = low[idx];
            bool pushed = false;
            for (auto to : out[idx]) {
                /**
                 * Only go to lower priority nodes, controlled by <pl>, that are not excluded or seen this round.
                 */
                if (disabled[i]) continue;
                if (to > i or done[to] == -2 or done[to] == pr or owner[to] != pl) continue;
                if (low[to] <= bot) {
                    // not visited, add to <st> and break!
                    st.push(to);
                    pushed = true;
                    break;
                } else {
                    // visited, update min
                    if (low[to] < min) min = low[to];
                }
            }
            if (pushed) continue; // we pushed...

            /**
             * If we're here, then there was no new edge and we check if we're the root of an SCC
             */
            if (min < low[idx]) {
                // not the root
                low[idx] = min;
                st.pop();
                continue;
            }

            /**
             * We're the root of an scc. Move the scc from <res> to <scc>.
             * Record highest prio, highest prio of good parity, highest node of good parity.
             */
            int max_pr = -1, max_pr_pl = -1, max_pr_n = -1;
            for (;;) {
                if (res.empty()) LOGIC_ERROR;
                int n = res.back();
                res.pop_back();
                scc.push_back(n);
                done[n] = pr; // dont check again this round
                if (low[n] != min) low[n] = min; // set it [for strategy]
                int d = priority[n];
                if (d > max_pr) max_pr = d;
                if ((d&1) == pl and d > max_pr_pl) {
                    max_pr_pl = d;
                    max_pr_n = n;
                }
                if (n == idx) break; // end when done
            }

            /**
             * Check if a single-node SCC without a self-loop
             */
            if (scc.size() == 1) {
                const auto &out_idx = out[idx];
                if (std::find(out_idx.begin(), out_idx.end(), idx) == out_idx.end()) {
                    // no self-loop
                    done[idx] = -2; // never check again
                    scc.clear();
                    st.pop();
                    continue;
                }
            }

            /**
             * Check if the highest priority in the SCC is actually won by <pl>.
             */

            if ((max_pr & 1) != pl) {
                for (auto n : scc) if (priority[n] > max_pr_pl) done[n] = -2; // never check again
                scc.clear();
                st.pop();
                continue;
                // Note that this SCC will be found again in lower runs, but without offending nodes.
            }

            /**
             * OK, got a winner!
             */

            if (trace) {
                logger << "winner-controlled scc with win priority \033[1;34m" << max_pr << "\033[m" << std::endl;
            }

            // Set strategies for all nodes in the SCC via backward search
            q.push(max_pr_n);
            while (!q.empty()) {
                int cur = q.front();
                q.pop();
                for (int from : in[cur]) {
                    if (low[from] != min or disabled[from]) continue;
                    solve(from, pl, cur); // also sets "disabled"
                    q.push(from);
                }
            }
            flush(); // this triggers attraction of every winner-controlled node in the stack...

            /**
             * For obvious reasons, all nodes on the stack are now solved.
             */
            while (!st.empty()) st.pop();
            res.clear();
            scc.clear();
            count++;
        }
    }

    delete[] done;
    delete[] low;
    return count;
}

int
Oink::solveSelfloops()
{
    int res = 0;
    for (int n=0; n<game->n_nodes; n++) {
        if (disabled[n]) continue;

        auto &out = game->out[n];
        for (auto it = out.begin(); it != out.end(); it++) {
            if (n != *it) continue;

            // found a self-loop
            if (game->owner[n] == (game->priority[n]&1)) {
                // self-loop is winning
                if (trace) logger << "winning self-loop with priority \033[1;34m" << game->priority[n] << "\033[m" << std::endl;
                solve(n, game->owner[n], n);
            } else {
                // self-loop is losing
                if (out.size() == 1) {
                    // it is a losing dominion
                    solve(n, 1 - game->owner[n], -1);
                } else {
                    // remove the edge
                    out.erase(it);
                    auto &in = game->in[n];
                    in.erase(std::remove(in.begin(), in.end(), n), in.end());
                    outcount[n]--;
                }
            }

            res++;
            break;
        }
    }

    flush();

    return res;
}

bool
Oink::solveSingleParity()
{
    int parity = -1;
    for (int i=0; i<game->n_nodes; i++) {
        if (disabled[i]) continue;
        if (parity == -1) {
            parity = game->priority[i]&1;
        } else if (parity == (game->priority[i]&1)) {
            continue;
        } else {
            return false;
        }
    }
    if (parity == 0 or parity == 1) {
        // solved with random strategy
        logger << "parity game only has parity " << (parity ? "odd" : "even") << std::endl;
        for (int i=0; i<game->n_nodes; i++) {
            if (disabled[i]) continue;
            if (game->owner[i] == parity) {
                // set random strategy for winner
                for (int to : game->out[i]) {
                    if (disabled[to]) continue;
                    solve(i, parity, to);
                    break;
                }
            } else {
                solve(i, parity, -1);
            }
        }
        flush();
        return true;
    } else {
        // all disabled
        return false;
    }
}

void
Oink::solve(int node, int win, int strategy)
{
    if (game->solved[node] or disabled[node]) LOGIC_ERROR;

    game->solved[node] = true;
    game->winner[node] = win;
    game->strategy[node] = (win == game->owner[node]) ? strategy : -1;
    disabled[node] = true; // disable
    todo.push(node);

    /*
    if (trace) {
        logger << "\033[1;32msolved " << (winner ? "(odd)" : "(even)") << "\033[m ";
        logger << "node " << node << "/" << game->priority[node];
        if (strategy != -1) logger << " to " << strategy << "/" << game->priority[strategy];
        logger << std::endl;
    }
    // */
}

void
Oink::flush()
{
    // flush the todo buffer
    while (todo.nonempty()) {
        int v = todo.pop();

        // check if we already did this node
        if (outcount[v] == -1) return;
        outcount[v] = -1; // mark it done

#ifndef NDEBUG
        assert(game->solved[v]);
#endif
        bool winner = game->winner[v];

        // base on ORIGINAL game in!
        for (int in : game->in[v]) {
            if (game->solved[in]) continue; // already done
            if (game->owner[in] == winner) {
                // node of winner
                game->strategy[in] = v;
                game->solved[in] = true;
                game->winner[in] = winner;
                disabled[in] = true;
                todo.push(in);
            } else {
                // node of loser
                if (--outcount[in] == 0) {
                    game->solved[in] = true;
                    game->winner[in] = winner;
                    disabled[in] = true;
                    todo.push(in);
                }
            }
        }
    }
}

void
Oink::setSolver(int solverid)
{
    solver = solverid;
}

void
Oink::setSolver(std::string label)
{
    solver = Solvers().id(label);
}

VOID_TASK_1(solve_loop, Oink*, s)
{
    s->solveLoop();
}

void
Oink::solveLoop()
{
    /**
     * Report chosen solver.
     */
    Solvers solvers;
    logger << "solving using " << solvers.desc(solver) << std::endl;

    while (!game->gameSolved()) {
        // disabled all solved vertices
        disabled = game->solved;

        if (bottomSCC) {
            // solve bottom SCC
            std::vector<int> sel;
            getBottomSCC(sel);
            assert(sel.size() != 0);
            disabled.set();
            for (int i : sel) disabled[i] = false;
            logger << "solving bottom SCC of " << sel.size() << " nodes (";
            logger << game->countUnsolved() << " nodes left)" << std::endl;
        }

        // solve current subgame
        Solver *s = solvers.construct(solver, this, game);
        s->run();
        delete s;

        // flush the todo buffer
        flush();

        // report number of nodes left
        if (!bottomSCC) {
            logger << game->countUnsolved() << " nodes left." << std::endl;
        }
    }
}

void
Oink::run()
{
    // NOTE: we assume that the game is already reindexed...

    /**
     * Now inflate / compress / renumber...
     */
    if (inflate) {
        int d = game->inflate();
        logger << "parity game inflated (" << d << " priorities)" << std::endl;
    } else if (compress) {
        int d = game->compress();
        logger << "parity game compressed (" << d << " priorities)" << std::endl;
    } else if (renumber) {
        int d = game->renumber();
        logger << "parity game renumbered (" << d << " priorities)" << std::endl;
    }

    /*
    // TODO this is for when we are provided a partial solution...
    // in case some nodes already have a dominion but are not yet disabled
    for (int i=0; i<game->n_nodes; i++) {
        if (game->solved[i] and !disabled[i]) {
            disabled[i] = true;
            todo.push_back(i);
        }
    }
    flush();
    */

    if (solveSingle and solveSingleParity()) return;

    if (removeLoops) {
        int count = solveSelfloops();
        if (count == 0) logger << "no self-loops removed" << std::endl;
        else if (count == 1) logger << "1 self-loops removed" << std::endl;
        else logger << count << " self-loops removed" << std::endl;
    }

    if (removeWCWC) {
        int count = solveTrivialCycles();
        if (count == 0) logger << "no trivial cycles removed" << std::endl;
        else if (count == 1) logger << "1 trivial cycle removed" << std::endl;
        else logger << count << " trivial cycles removed" << std::endl;
    } else if (Solvers().label(solver) == "psi") {
        logger << "\033[1;7mWARNING\033[m: running PSI solver without removing winner-controlled winning cycles!" << std::endl;
    }

    if (solver == -1) {
        logger << "no solver selected" << std::endl;
        return;
    }

    /***
     * Build arrays
     */
    {
        // count number of edges
        size_t len = game->edgecount() + game->n_nodes;

        outa = new int[game->n_nodes];
        ina = new int[game->n_nodes];
        outs = new int[len];
        ins = new int[len];

        int outi = 0;
        int ini = 0;

        for (int i=0; i<game->n_nodes; i++) {
            outa[i] = outi;
            ina[i] = ini;
            for (int to : game->out[i]) outs[outi++] = to;
            for (int fr : game->in[i]) ins[ini++] = fr;
            outs[outi++] = -1;
            ins[ini++] = -1;
        }
    }

    /***
     * Start Lace if we are parallel
     */

    if (Solvers().isParallel(solver)) {
        if (workers >= 0) {
            if (lace_workers() == 0) {
                lace_init(workers, 100*1000*1000);
                logger << "initialized Lace with " << lace_workers() << " workers" << std::endl;
                lace_startup(0, (lace_startup_cb)TASK(solve_loop), this);
            } else {
                logger << "running parallel (Lace already initialized)" << std::endl;
                solveLoop();
            }
        } else {
            logger << "running sequentially" << std::endl;
            solveLoop();
        }
    } else {
        solveLoop();
    }

    delete[] outa;
    delete[] ina;
    delete[] outs;
    delete[] ins;
}

}
