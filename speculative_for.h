// This code is part of the Problem Based Benchmark Suite (PBBS)
// Copyright (c) 2011 Guy Blelloch and the PBBS team
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights (to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "parallel.h"
#include "sequence.h"
#include <limits>

constexpr intT max_int = std::numeric_limits<intT>::max();
  
struct reservation {
  intT r;
  reservation() : r(max_int) {}
  void reserve(intT i) { pbbs::write_min(&r, i, std::less<intT>());}
  bool reserved() { return (r < max_int);}
  void reset() {r = max_int;}
  bool check(intT i) { return (r == i);}
  bool checkReset(intT i) {
    if (r==i) { r = max_int; return 1;}
    else return 0;
  }
};

inline void reserveLoc(intT& x, intT i) {
  pbbs::write_min(&x,i, std::less<intT>());}

template <class S>
long speculative_for(S step, long s, long e, long granularity, 
		     bool hasState=1, long maxTries=-1) {
  if (maxTries < 0) maxTries = 100 + 200*granularity;
  long maxRoundSize = (e-s)/granularity+1;
  long currentRoundSize = maxRoundSize;
  pbbs::sequence<intT> I(maxRoundSize);
  pbbs::sequence<bool> keep(maxRoundSize);
  pbbs::sequence<intT> Ihold;
  pbbs::sequence<S> state;
  if (hasState) 
    state = pbbs::sequence<S>(maxRoundSize, [&] (size_t i) {return step;});

  long round = 0; 
  long numberDone = s; // number of iterations done
  long numberKeep = 0; // number of iterations to carry to next round
  long totalProcessed = 0; // number done including wasteds tries

  while (numberDone < e) {
    if (round++ > maxTries) {
      cout << "speculative_for: too many iterations, increase maxTries"<< endl;
      abort();
    }
    //long size = std::max(std::min(currentRoundSize, e - numberDone + numberKeep),
    //   numberKeep);
    long size = std::min(currentRoundSize, e - numberDone);

    totalProcessed += size;
    size_t loop_granularity = 0;

    if (hasState) {
      parallel_for (0, size, [&] (size_t i) {
	  I[i] = (i < numberKeep) ? Ihold[i] : numberDone + i;
	  keep[i] = state[i].reserve(I[i]);
	}, loop_granularity);
    } else {
      parallel_for (0, size, [&] (size_t i) {
	  I[i] = (i < numberKeep) ? Ihold[i] : numberDone + i;
	  keep[i] = step.reserve(I[i]);
	}, loop_granularity);
    }

    if (hasState) {
      parallel_for (0, size, [&] (size_t i) {
	  if (keep[i]) keep[i] = !state[i].commit(I[i]);}, loop_granularity);
    } else {
      parallel_for (0, size, [&] (size_t i) {
	  if (keep[i]) keep[i] = !step.commit(I[i]);}, loop_granularity);
    }

    // keep iterations that failed for next round
    Ihold = pbbs::pack(I.slice(0,size), keep.slice(0,size));
    numberKeep = Ihold.size();
    numberDone += size - numberKeep;

    // adjust round size based on number of failed attempts
    if (float(numberKeep)/float(size) > .2) 
      currentRoundSize = std::max(currentRoundSize/2, 
				  std::max(maxRoundSize/64 + 1, numberKeep));
    else if (float(numberKeep)/float(size) < .1) 
      currentRoundSize = std::min(currentRoundSize * 2, maxRoundSize);
    //cout << size << " : " << numberKeep << " : " << numberDone << endl;
  }
  return totalProcessed;
}
