/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2017 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cassert>
#include <cstddef>
#include <iostream>

#include "bitboard.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

#include <mpi.h>

namespace PSQT {
  void init();
}

int mpi_rank;
int mpi_size;
MPI_Datatype mpi_tte_t;
MPI_Datatype mpi_cluster_t;

void init_mpi(int *argc, char ***argv) {
  // Don't include padding in cluster type, since we can leave it uninitialized
  int threading,
      tte_blocklengths[] = {1, 1, 1, 1, 1, 1},
      cluter_blocklengths[] = {ClusterSize, 1};
  MPI_Aint tte_displacements[6], cluster_displacements[] = {
    offsetof(Cluster, entry),
    offsetof(Cluster, key)
  };
  MPI_Datatype tte_types[] = {
    MPI_UINT16_T,
    MPI_UINT16_T,
    MPI_INT16_T,
    MPI_INT16_T,
    MPI_UINT8_T,
    MPI_INT8_T
  }, cluster_types[2];

  MPI_Init_thread(argc, argv, MPI_THREAD_MULTIPLE, &threading);
  assert(threading == MPI_THREAD_MULTIPLE);

  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);

  TTEntry::fill_displacements(tte_displacements);
  MPI_Type_create_struct(6, tte_blocklengths, tte_displacements, tte_types,
    &mpi_tte_t);
  MPI_Type_commit(&mpi_tte_t);
  cluster_types[0] = mpi_tte_t;
  cluster_types[1] = MPI_UINT16_T;
  MPI_Type_create_struct(2, cluter_blocklengths, cluster_displacements,
    cluster_types, &mpi_cluster_t);
  MPI_Type_commit(&mpi_cluster_t);
}

int main(int argc, char* argv[]) {
  init_mpi(&argc, &argv);

  if (mpi_rank == 0) {
    std::cout << engine_info() << std::endl;
  }

  UCI::init(Options);
  PSQT::init();
  Bitboards::init();
  Position::init();
  Bitbases::init();
  Search::init();
  Pawns::init();
  Threads.init();
  Tablebases::init(Options["SyzygyPath"]);
  TT.resize(Options["Hash"]);

  UCI::loop(argc, argv);

  Threads.exit();

  MPI_Finalize();

  return 0;
}
