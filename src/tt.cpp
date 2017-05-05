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

#include <cstdlib>
#include <cstring>   // For std::memset
#include <iostream>

#include "bitboard.h"
#include "tt.h"

TranspositionTable TT; // Our global transposition table

MPI_Win tt_win;

/// TranspositionTable::resize() sets the size of the transposition table,
/// measured in megabytes. Transposition table consists of a power of 2 number
/// of clusters and each cluster consists of ClusterSize number of TTEntry.

void TranspositionTable::resize(size_t mbSize) {

  size_t newClusterCount = size_t(1) << msb((mbSize * 1024 * 1024) / sizeof(Cluster));

  if (newClusterCount == clusterCount)
      return;

  clusterCount = newClusterCount;

  mem = malloc(clusterCount * sizeof(Cluster) + CacheLineSize - 1);

  if (!mem)
  {
      std::cerr << "Failed to allocate " << mbSize
                << "MB for transposition table." << std::endl;
      exit(EXIT_FAILURE);
  }

  table = (Cluster*)((uintptr_t(mem) + CacheLineSize - 1) & ~(CacheLineSize - 1));
  memset(table, 0, clusterCount * sizeof(Cluster));

  for (size_t i = 0; i < clusterCount; ++i) {
    table[i].key = i;
  }
}


/// TranspositionTable::clear() overwrites the entire transposition table
/// with zeros. It is called whenever the table is resized, or when the
/// user asks the program to clear the table (from the UCI interface).

void TranspositionTable::clear() {

  abort();
  std::memset(table, 0, clusterCount * sizeof(Cluster));
}


/// TranspositionTable::probe() looks up the current position in the transposition
/// table. It returns true and a pointer to the TTEntry if the position is found.
/// Otherwise, it returns false and a pointer to an empty or least valuable TTEntry
/// to be replaced later. The replace value of an entry is calculated as its depth
/// minus 8 times its relative age. TTEntry t1 is considered more valuable than
/// TTEntry t2 if its replace value is greater than that of t2.

void DistTTEntry::save(Cluster &cluster_buf) {
  assert(cluster_buf.key == key);
  if (rank == mpi_rank) {
    return;
  }

  put_t put;
  put.key = key;
  put.tte = *tte;

  std::cout << mpi_rank << " put1 " << rank << std::endl;
  MPI_Send(&put, 1, mpi_put_t, rank, 2, MPI_COMM_WORLD);
  std::cout << mpi_rank << " put2 " << rank << std::endl;
}

DistTTEntry TranspositionTable::probe(const Key key, bool& found, Cluster& cluster_buf) const {
  DistTTEntry dtte;
  const uint16_t key16 = key >> 48;  // Use the high 16 bits as key inside the cluster
  const int owner_rank = ((key >> 45) & 7) % mpi_size;
  TTEntry* tte;
  const size_t cluster_key = (size_t)key & (clusterCount - 1);

  cluster_buf.key = cluster_key;
  dtte.key = cluster_key;
  dtte.rank = owner_rank;

  if (owner_rank == mpi_rank) {
    tte = first_entry(key);
    assert(table[cluster_key].key == dtte.key);
  } else {
    std::cout << mpi_rank << " get1 " << owner_rank << std::endl;
    MPI_Send(&key, 1, MPI_UINT64_T, owner_rank, 1, MPI_COMM_WORLD);
    std::cout << mpi_rank << " get2 " << owner_rank << std::endl;
    tte = &cluster_buf.entry[0];
    MPI_Recv(tte, 1, mpi_tte_t, owner_rank, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    std::cout << mpi_rank << " get3 " << owner_rank << std::endl;
    // assert(cluster_buf.key == dtte.key);
    dtte.tte = tte;
    return found = (bool)tte->key16, dtte;
  }

  for (int i = 0; i < ClusterSize; ++i)
      if (!tte[i].key16 || tte[i].key16 == key16)
      {
          if ((tte[i].genBound8 & 0xFC) != generation8 && tte[i].key16)
              tte[i].genBound8 = uint8_t(generation8 | tte[i].bound()); // Refresh

          dtte.tte = &tte[i];
          return found = (bool)tte[i].key16, dtte;
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

  dtte.tte = replace;
  return found = false, dtte;
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
