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

#include <algorithm>
#include <cstring>   // For std::memset
#include <iostream>

#include "bitboard.h"
#include "tt.h"
#include "search.h"

TranspositionTable TT; // Our global transposition table


/// TranspositionTable::resize() sets the size of the transposition table,
/// measured in megabytes. Transposition table consists of a power of 2 number
/// of clusters and each cluster consists of ClusterSize number of TTEntry.

void TranspositionTable::resize(size_t mbSize) {

  size_t newClusterCount = size_t(1) << msb((mbSize * 1024 * 1024) / sizeof(Cluster));

  if (newClusterCount == clusterCount)
      return;

  clusterCount = newClusterCount;

  free(mem);
  mem = calloc(clusterCount * sizeof(Cluster) + CacheLineSize - 1, 1);

  if (!mem)
  {
      std::cerr << "Failed to allocate " << mbSize
                << "MB for transposition table." << std::endl;
      exit(EXIT_FAILURE);
  }

  table = (Cluster*)((uintptr_t(mem) + CacheLineSize - 1) & ~(CacheLineSize - 1));
}


/// TranspositionTable::clear() overwrites the entire transposition table
/// with zeros. It is called whenever the table is resized, or when the
/// user asks the program to clear the table (from the UCI interface).

void TranspositionTable::clear() {

  std::memset(table, 0, clusterCount * sizeof(Cluster));
}


/// TranspositionTable::probe() looks up the current position in the transposition
/// table. It returns true and a pointer to the TTEntry if the position is found.
/// Otherwise, it returns false and a pointer to an empty or least valuable TTEntry
/// to be replaced later. The replace value of an entry is calculated as its depth
/// minus 8 times its relative age. TTEntry t1 is considered more valuable than
/// TTEntry t2 if its replace value is greater than that of t2.

TTEntry* TranspositionTable::probe(const Key key, bool& found) const {

  TTEntry* const tte = first_entry(key);
  const uint16_t key16 = key >> 48;  // Use the high 16 bits as key inside the cluster

  for (int i = 0; i < ClusterSize; ++i)
      if (!tte[i].key16 || tte[i].key16 == key16)
      {
          if ((tte[i].genBound8 & 0xFC) != generation8 && tte[i].key16)
              tte[i].genBound8 = uint8_t(generation8 | tte[i].bound()); // Refresh

          found = (bool)tte[i].key16;
          if (found) {
            ++table[key & (clusterCount - 1)].padding;
          }
          return &tte[i];
      }

  // Find an entry to be replaced according to the replacement strategy
  TTEntry* replace = tte;
  for (int i = 1; i < ClusterSize; ++i)
      // Due to our packed storage format for generation and its cyclic
      // nature we add 259 (256 is the modulus plus 3 to keep the lowest
      // two bound bits from affecting the result) to calculate the entry
      // age correctly even after generation8 overflows into the next cycle.
      if (  replace->depth8 - ((259 + generation8 - replace->genBound8) & 0xFC) * 2
          >   tte[i].depth8 - ((259 + generation8 -   tte[i].genBound8) & 0xFC) * 2)
          replace = &tte[i];

  return found = false, replace;
}


/// TranspositionTable::hashfull() returns an approximation of the hashtable
/// occupation during a search. The hash is x permill full, as per UCI protocol.

int TranspositionTable::hashfull() const {

  int cnt = 0;
  for (int i = 0; i < 1000 / ClusterSize; i++)
  {
      const TTEntry* tte = &table[i].entry[0];
      for (int j = 0; j < ClusterSize; j++)
          if ((tte[j].genBound8 & 0xFC) == generation8)
              cnt++;
  }
  return cnt;
}

void TranspositionTable::clusterOp(void *invec_, void *inoutvec_,
  int *len, MPI_Datatype *type) {
  (void)type;
  Cluster *invec = (Cluster *)invec_, *inoutvec = (Cluster *)inoutvec_;
  int values[ClusterSize * 2];
  TTEntry ttes[ClusterSize * 2];
  int a[ClusterSize * 2];
  const int gen = TT.generation();

  for (int i = 0; i < *len; ++i) {
    inoutvec[i].padding += invec[i].padding;
    TTEntry *intte = &invec[i].entry[0], *inouttte = &inoutvec[i].entry[0];
    memcpy(&ttes[0], inouttte, sizeof(TTEntry) * ClusterSize);
    memcpy(&ttes[ClusterSize], intte, sizeof(TTEntry) * ClusterSize);
    for (int j = 0; j < ClusterSize; ++j) {
      values[j] = intte[j].depth8 - ((259 + gen - intte[j].genBound8) & 0xFC) *
        2;
    }
    for (int j = 0; j < ClusterSize; ++j) {
      values[j + ClusterSize] = inouttte[j].depth8 - ((259 + gen -
          inouttte[j].genBound8) & 0xFC) * 2;
    }
    for (int j = 0; j < ClusterSize * 2; ++j) {
      a[j] = j;
    }
    std::sort(&a[0], &a[ClusterSize * 2], [&] (int x, int y) {
      return values[x] > values[y];
    });
    for (int j = 0; j < ClusterSize; ++j) {
      memcpy(&inouttte[j], &ttes[a[j]], sizeof(TTEntry));
    }
  }
}

namespace Search {
  extern SignalsType Signals;
}

void TranspositionTable::updateLoop() {
  static const int MaxNeeded = 2;
  static const int Batch = 256;

  uint16_t counts[Batch + 1] = {0};
  Cluster c[Batch];
  memset(&c[0], 0, sizeof(c));

  for (;;) {
    for (size_t i = 0; i < clusterCount / Batch; ++i) {

      // for (int j = 0; j < Batch; ++j) {
      //   Cluster *d = &table[i * Batch + j];
      //   counts[j] = d->padding + !!d->entry[0].key16 + !!d->entry[1].key16 +
      //     !!d->entry[2].key16;
      // }

      if (Search::Signals.stop) {
        counts[Batch] = 1;
      }
      // MPI_Allreduce(MPI_IN_PLACE, &counts[0], Batch, MPI_UINT16_T, MPI_MAX,
      //   MPI_COMM_WORLD);

      if (counts[Batch] >= 1) {
        return;
      }

      // int m = 0;
      // for (int j = 0; j < Batch; ++j) {
      //   // if (counts[j] < MaxNeeded) {
      //   //   continue;
      //   // }
      //   memcpy(&c[m++], &table[i * Batch + j], sizeof(Cluster));
      // }
      memcpy(&c[0], &table[i * Batch], sizeof(c));
      MPI_Allreduce(MPI_IN_PLACE, &c[0], Batch, mpi_cluster_t, cluster_op,
        MPI_COMM_WORLD);
      memcpy(&table[i * Batch], &c[0], sizeof(c));

      // for (int j = Batch - 1; j >= 0; --j) {
      //   // if (counts[j] < MaxNeeded) {
      //   //   continue;
      //   // }
      //   memcpy(&c[--m], &table[i * Batch + j], sizeof(Cluster));
      // }
    }

    if (mpi_rank == 0) {
      std::cout << "DONE" << std::endl;
    }
  }
}
