// This code is part of the Problem Based Benchmark Suite (PBBS)
// Copyright (c) 2010-2017 Guy Blelloch and the PBBS team
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

#pragma once
#include <math.h>
#include <stdio.h>
#include <cstdint>
#include "utilities.h"
#include "counting_sort.h"
#include "quicksort.h"

namespace pbbs {

  constexpr size_t radix = 8;
  constexpr size_t max_buckets = 1 << radix;

  // a bottom up radix sort
  template <class Slice, class GetKey>
  void seq_radix_sort_(Slice In, Slice Out, GetKey const &g,
		       size_t bits, bool inplace=true) {
    size_t n = In.size();
    if (n == 0) return;
    size_t counts[max_buckets+1];
    bool swapped = false;
    int bit_offset = 0;
    while (bits > 0) {
      size_t round_bits = std::min(radix, bits);
      size_t num_buckets = (1 << round_bits);
      size_t mask = num_buckets-1;
      auto get_key = [&] (size_t i) -> size_t {
	return (g(In[i]) >> bit_offset) & mask;};
      seq_count_sort_(In, Out, delayed_seq<size_t>(n, get_key),
		      counts, num_buckets);
      std::swap(In,Out);
      bits = bits - round_bits;
      bit_offset = bit_offset + round_bits;
      swapped = !swapped;
    }
    if ((inplace && swapped) || (!inplace && !swapped)) {
      for (size_t i=0; i < n; i++) 
	move_uninitialized(Out[i], In[i]);
    }
  }

  // wrapper to reduce copies and avoid modifying In when not inplace
  template <class SeqIn, class Slice, class GetKey>
  void seq_radix_sort(SeqIn const &In, Slice Out, Slice Tmp, GetKey const &g,
		      size_t key_bits, bool inplace=true) {
    bool odd = ((key_bits-1)/radix) & 1;
    size_t n = In.size();
    if (slice_eq(In.slice(), Tmp)) { // not inplace
      if (odd) {
	for (size_t i=0; i < n; i++) 
	  move_uninitialized(Tmp[i], In[i]);
	seq_radix_sort_(Tmp, Out, g, key_bits, false);
      } else {
	for (size_t i=0; i < n; i++) 
	  move_uninitialized(Out[i], In[i]);
	seq_radix_sort_(Out, Tmp, g, key_bits, true);
      }
    }	else
      seq_radix_sort_(In.slice(), Out, g, key_bits, inplace);
  }

  // not used, can delete
  template <typename SeqIn, typename Slice, typename Get_Key>
  sequence<size_t> integer_sort_2(SeqIn const &In, Slice Out, Get_Key const &g, 
				  size_t key_bits, bool is_nested=false) {
    using T = typename SeqIn::value_type;
    size_t n = In.size();
    timer t;
    size_t high_bits = key_bits/2;
    size_t high_buckets = (1 << high_bits);
    size_t high_mask = high_buckets - 1;
    size_t low_bits = key_bits-high_bits;
    size_t low_buckets = (1 << low_bits);
    size_t low_mask = low_buckets - 1;
    auto fh = [&] (size_t i) {return (g(In[i]) >> low_bits) & high_mask;};
    auto get_high_bits = delayed_seq<size_t>(n, fh);
    sequence<T> Tmp(n);

    // divide into buckets
    sequence<size_t> offsets = count_sort(In.slice(), Tmp.slice(),
					  get_high_bits, high_buckets,
					  is_nested);

    sequence<size_t> result_offsets(high_buckets*low_buckets);
    // recursively sort each bucket
    parallel_for(0, high_buckets, [&] (size_t i) {
	size_t start = offsets[i];
	size_t end = offsets[i+1];
	auto A = Tmp.slice(start, end);
	auto B = Out.slice(start, end);
	auto fl = [&] (size_t i) {return g(A[i]) & low_mask;};
	auto get_low_bits = delayed_seq<size_t>(end-start, fl);
	sequence<size_t> oin = count_sort(A, B, get_low_bits,
					  low_buckets, true);
	parallel_for(0, low_buckets, [&] (size_t j) {
	    result_offsets[i*high_buckets + j] = oin[j];});
      }, 1);
    return result_offsets;
  }

  // a top down recursive radix sort
  // g extracts the integer keys from In
  // key_bits specifies how many bits there are left
  // if inplace is true, then result will be in Tmp, otherwise in Out
  // In and Out cannot be the same, but In and Tmp should be same if inplace
  template <typename SeqIn, typename Slice, typename Get_Key>
  void integer_sort_r(SeqIn const &In, Slice Out, Slice Tmp, Get_Key const &g, 
		      size_t key_bits, bool inplace, bool is_nested=false) {
    size_t n = In.size();
    timer t("integer sort",false);

    if (key_bits == 0) {
      if (!inplace)
	parallel_for(0, In.size(), [&] (size_t i) {Out[i] = In[i];});

      // for small inputs use sequential radix sort
    } else if (n < (1 << 15)) {
      seq_radix_sort(In, Out, Tmp, g, key_bits, inplace);
      
      // few bits, just do a single parallel count sort
    } else if (key_bits <= radix) {
      size_t num_buckets = (1 << key_bits);
      size_t mask = num_buckets - 1;
      auto f = [&] (size_t i) {return g(In[i]) & mask;};
      auto get_bits = delayed_seq<size_t>(n, f);
      count_sort(In.slice(), Out, get_bits, num_buckets, is_nested);
      if (inplace)
	parallel_for(0, n, [&] (size_t i) {
	    move_uninitialized(In[i], Out[i]);});
      
      // recursive case  
    } else {
      size_t bits = 8;
      size_t shift_bits = key_bits - bits;
      size_t buckets = (1 << bits);
      size_t mask = buckets - 1;
      auto f = [&] (size_t i) {return (g(In[i]) >> shift_bits) & mask;};
      auto get_bits = delayed_seq<size_t>(n, f);

      // divide into buckets
      sequence<size_t> offsets = count_sort(In.slice(), Out, get_bits, buckets, is_nested);
      if (n > 10000000) t.next("first");
      // recursively sort each bucket
      parallel_for(0, buckets, [&] (size_t i) {
	  size_t start = offsets[i];
	  size_t end = offsets[i+1];
	  auto a = Out.slice(start, end);
	  auto b = Tmp.slice(start, end);
	  integer_sort_r(a, b, a, g, shift_bits, !inplace, true);
	}, 1);
      if (n > 10000000) t.next("second");
    }
  }

  // a top down recursive radix sort
  // g extracts the integer keys from In
  // if inplace is false then result will be placed in Out, 
  //    otherwise they are placed in Tmp
  //    Tmp and In can be the same (i.e. to do inplace set them equal)
  // In is not directly modified, but can be indirectly if equal to Tmp
  // val_bits specifies how many bits there are in the key
  //    if set to 0, then a max is taken over the keys to determine
  template <typename SeqIn, typename IterOut, typename Get_Key>
  void integer_sort_(SeqIn const &In,
		     range<IterOut> Out,
		     range<IterOut> Tmp,
		     Get_Key const &g, 
		     size_t key_bits=0,
		     bool inplace=false) {
    using T = typename SeqIn::value_type;
    if (slice_eq(In.slice(), Out)) {
      cout << "in integer_sort : input and output must be different locations"
	   << endl;
      abort();}
    if (key_bits == 0) {
      using P = std::pair<size_t,size_t>;
      auto get_key = [&] (size_t i) -> P {
	size_t k =g(In[i]);
	return P(k,k);};
      auto keys = delayed_seq<P>(In.size(), get_key);
      size_t min_val, max_val;
      std::tie(min_val,max_val) = reduce(keys, minmaxm<size_t>());
      key_bits = log2_up(max_val - min_val + 1);
      if (min_val > max_val / 4) {
	auto h = [&] (T a) {return g(a) - min_val;};
	integer_sort_r(In, Out, Tmp, h, key_bits, inplace);
	return;
      }
    }
    integer_sort_r(In, Out, Tmp, g, key_bits, inplace);
  }

  template <typename T, typename Get_Key>
  void integer_sort_inplace(range<T*> In,
			    Get_Key const &g,
			    size_t key_bits=0) {
    sequence<T> Tmp = sequence<T>::no_init(In.size());
    integer_sort_(In, Tmp.slice(), In, g, key_bits, true);
  }

  template <typename Seq, typename Get_Key>
  sequence<typename Seq::value_type> integer_sort(Seq const &In, Get_Key const &g,
						  size_t key_bits=0) {
    using T = typename Seq::value_type;
    sequence<T> Out = sequence<T>::no_init(In.size());
    sequence<T> Tmp = sequence<T>::no_init(In.size());
    integer_sort_(In, Out.slice(), Tmp.slice(), g, key_bits, false);
    return Out;
  }
}
