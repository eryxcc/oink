#include <algorithm>
#include <sstream>
#include <vector>
#include <queue>
#include <cassert>
#include <map>

#include "experimental.hpp"
#include "printf.hpp"

namespace pg {

ExperimentalSolver::ExperimentalSolver(Oink *oink, Game *game, int flags) : Solver(oink, game), flags(flags)
{
}

ExperimentalSolver::~ExperimentalSolver()
{
}

int myindent = 0;

struct autoindent {
  autoindent() { myindent += 2; }
  ~autoindent() { myindent -= 2; }
  };

#define DEB(x) // x

int last_category = 0;

int new_category() { return last_category++; }

struct zsolver {
  int iters = 0;
  std::ostream& logger;
  zsolver(std::ostream& l) : logger(l) {}
  
  Game *g;
  int flags;
  std::vector<int> vtype;
  std::vector<int> strategy;
  std::vector<int> degs;
  
  // Find the attractor in the subgame.
  // vs: list of vertices in the subgame
  // precondition: vtype[v] is cat_no or cat_yes iff v \in vs
  // postcondition: vertices from where we can get to cat_yes without leaving vs
  // are marked as cat_yes too
  // postcondition: strategy[v] is the correct move for v \in vs, or -1 if losing
  // postcondition: vtype and strategy do not change outside of vs
  void attractor(const std::vector<int>& vs, int whose, int cat_no, int cat_yes) {

    static std::vector<int> aqueue;
    aqueue.clear();
    degs.resize(g->n_nodes, -1);
    for(auto v: vs) 
      if(vtype[v] == cat_yes)
        aqueue.push_back(v);
      else if(g->owner[v] == whose)
        degs[v] = 1;
      else {
        degs[v] = 0;
        for(auto w: g->out[v]) if(vtype[w] == cat_no || vtype[w] == cat_yes) degs[v]++;
        }
      
    // invariant: degs[v] is the number of edges from v which we need to prove that
    // they to lead to cat_yes, in order to make v also in cat_yes
    // a vertex is 'proven' if it has been already considered as v below
    for(int i=0; i < int(aqueue.size()); i++) {
      int v = aqueue[i];
      for(auto w: g->in[v]) {
        degs[w]--;
        if(degs[w] == 0) {
        
          vtype[w] = cat_yes;
          strategy[w] = g->owner[w] == whose ? v : -1;
          aqueue.push_back(w);
          }
        }
      }
    
    for(auto v: vs) degs[v] = -1;
    }

  // Solve a subgame.
  // vs: list of vertices in the subgame
  // precondition: vtype[v] is cat_base iff v \in vs
  // postcondition: strategy[v] is the correct move for v \in vs, or -1 if losing
  // postcondition: vtype and strategy do not change outside of vs
  // mode == 0 : first pass with reduced precision
  // mode == 1 : second pass with full precision
  // mode == 2 : third pass with reduced precision
  // mode == 3 : run standard ZLK
  void run(const std::vector<int>& vs, int cat_base, std::array<int, 2> precision, int mode, int mprio);
  };

std::map<std::pair<std::array<int, 2>, std::vector<int>>, std::vector<int> > memo;

void zsolver::run(const std::vector<int>& vs, int cat_base, std::array<int, 2> precision, int mode, int mprio) {
  if((flags & memoize) && memo.count({precision, vs})) {
    auto& m = memo[{precision, vs}];
    for(int i=0; i<int(vs.size()); i++)
      strategy[vs[i]] = m[i];
    return;
    }
  iters++;
  if(vs.size() == 0) return;

  DEB(
  for(int i=0; i<myindent; i++) logger << " ";
  logger << "Vertices:"; for(auto v: vs) logger << " " << v; 
  logger << ", precision = " << precision[0] << "," << precision[1];
  logger << ", mode = " << mode;
  logger << ", mprio = " << mprio;
  logger << "\n";
  )
  
  DEB( autoindent ai; )
  if(true) {
    int maxprio = mprio;
    if(mprio < 0) {  
      for(auto v: vs) maxprio = std::max(maxprio, g->priority[v]);
      }

    int us = (maxprio&1);
    int opponent = us^1;

    if(precision[us] <= 0) {
      for(auto v: vs) {
        if(g->owner[v] == (maxprio&1)) strategy[v] = -1;
        else strategy[v] = 999;
        }
      return;
      }
    
    int cat_hiprio = new_category();

    for(auto v: vs) if(g->priority[v] == maxprio)
      vtype[v] = cat_hiprio, strategy[v] = -2;
    
    attractor(vs, us, cat_base, cat_hiprio);

    auto subprecision = precision;
    if(mode == 0 || mode == 2) subprecision[opponent]--;

    std::vector<int> subgame;
    for(auto v: vs) if(vtype[v] == cat_base) subgame.push_back(v);
    
    if(subprecision[opponent] == 0) {
      for(auto v: vs) {
        if(g->owner[v] == us) strategy[v] = 999;
        else strategy[v] = -1;
        }
      }
    else
      run(subgame, cat_base, subprecision, mode == 3 ? 3 : 0, mprio-1);
                                       
    // vtype 3: opponent surely wins
    bool subgame_won = true;
    int cat_opponent_wins = new_category();
    for(auto v: subgame) 
      if(g->owner[v] == us ? strategy[v] == -1 : strategy[v] >= 0)
        vtype[v] = cat_opponent_wins, subgame_won = false;
      else 
        vtype[v] = cat_hiprio;

    if(subgame_won) {
    
      if(mode == 0) {
        run(vs, cat_hiprio, precision, 1, mprio);
        return;
        }

      // strategy not specified for maxprio yet
      for(auto v: vs) if(g->priority[v] == maxprio) {
        if(g->owner[v] == us) {
          for(auto e: g->out[v]) if(vtype[e] == cat_hiprio) strategy[v] = e;
          }
        else 
          strategy[v] = -1;
        }

      DEB( for(int i=0; i<myindent; i++) logger << " ";
      logger << "Strategy:"; for(int i=0; i<g->n_nodes; i++) if(vtype[i] == 0) logger << " ?"; else logger << " " << strategy[i]; logger << "\n";
      )

      if(flags & memoize) {
        auto& m = memo[{precision, vs}];
        for(int i=0; i<int(vs.size()); i++)
          m.push_back(strategy[vs[i]]);
        }

      return;
      }

    attractor(vs, opponent, cat_hiprio, cat_opponent_wins);

    subgame.clear();
    for(auto& v: vs) if(vtype[v] == cat_hiprio) subgame.push_back(v);

    run(subgame, cat_hiprio, precision, mode == 1 ? 2 : mode, mprio);

    if(flags & memoize) {
      auto& m = memo[{precision, vs}];
      for(int i=0; i<int(vs.size()); i++)
        m.push_back(strategy[vs[i]]);
      }

DEB(    for(int i=0; i<myindent; i++) logger << " ";
    logger << "Strategy:"; for(int i=0; i<g->n_nodes; i++) if(vtype[i] == 0) logger << " ?"; else logger << " " << strategy[i]; logger << "\n";  )
    
DEB(
    for(auto v: vs) if(strategy[v] == -1) {
      for(auto e: g->out[v]) if(g->owner[v] == g->owner[e] && strategy[e] >= 0)
        logger << "Escape A\n";
      for(auto e: g->out[v]) if(g->owner[v] != g->owner[e] && strategy[e] == -1)
        logger << "Escape B\n";
      } )
    }
  }

void ExperimentalSolver::run()
{
    zsolver zs(logger);
    zs.g = game;
    zs.strategy.resize(n_nodes);
    zs.vtype.resize(n_nodes);
    zs.flags = flags;
    int cat = new_category();
    for(int& i: zs.vtype) i = cat;
    std::vector<int> vset;
    for(int i=0; i<n_nodes; i++)
      vset.push_back(i);
    
    logger << "N = " << n_nodes << std::endl;
/*
    for (int i=0; i<n_nodes; i++) {
      fmt::printf(logger, "Vertex %d: owner %d, priority %d\n", i, (int) owner[i], (int) priority[i]);
      for(auto e: out[i])
        fmt::printf(logger, "Out-edge to %d\n", (int) e);
      for(auto e: in[i])
        fmt::printf(logger, "In-edge from %d\n", (int) e);
      } */

    int prec = 0;
    while((1 << prec) < n_nodes) prec++;
    
    fmt::printf(logger, "initial precision = %d\n", prec);

    int maxprio = 0;
    for(auto v: vset) maxprio = std::max(maxprio, priority[v]);

    fmt::printf(logger, "max priority = %d\n", maxprio);

    zs.iters = 0;
    zs.run(vset, cat, {prec, prec}, (flags&zielonka) ? 3 : 0, (flags&quick_priority)?-1:maxprio);

    fmt::printf(logger, "solved in %d iterations\n", zs.iters);

    for (int i=0; i<n_nodes; i++) if(!game->solved[i]) {
        DEB( fmt::printf(logger, "%d -> %d\n", i, zs.strategy[i]); )
        if(zs.strategy[i] >= 0)
          oink->solve(i, owner[i], zs.strategy[i]);
        else
          oink->solve(i, !owner[i], -1);
        }
}

}
