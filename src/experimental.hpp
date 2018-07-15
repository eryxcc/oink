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

#ifndef ERYX_HPP
#define ERYX_HPP

#include <queue>

#include "solver.hpp"
#include "uintqueue.hpp"

namespace pg {

static const int zielonka = 1;
static const int memoize = 2;
static const int quick_priority = 4;
static const int auto_reduce = 8;

class ExperimentalSolver : public Solver
{
public:
    ExperimentalSolver(Oink *oink, Game *game, int flags);
    virtual ~ExperimentalSolver();

    virtual void run();
    int flags;
};

}

#endif 
