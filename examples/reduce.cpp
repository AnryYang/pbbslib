#include "sequence.h"
#include "get_time.h"
#include "strings/string_basics.h"
#include "parse_command_line.h"
#include "group_by.h"
#include<time.h>
#include<sys/time.h>

// g++ -I ../ -mcx16 -O3 -std=c++17 -march=native -Wall  -DHOMEGROWN -pthread reduce.cpp -o reduce
using namespace pbbs;

int main (int argc, char *argv[]) {
  timer t("summation", true);

  size_t n=10000000000;
  int* arr = new int[n];
  for (size_t i=0; i < n; i++) {
    arr[i]=i%3;
  }
  //t.next("create integer array");

  using T = int;
  auto A = delayed_seq<T>(n, [&] (size_t i) {return (T) (arr[i]);});
  auto f = [&] (T a, T b){
      return T(a+b);
  };
  auto m = make_monoid(f, T(0));

  struct timeval start, end;
  gettimeofday(&start, NULL);
  T res1 = reduce_serial(A, m);
  //T res1 = 0;
  //for(size_t i=0; i<n; i++){
  //    res1 += arr[i];
 // }
  //t.next("serial sum");
  cout << res1 << endl;
  gettimeofday(&end, NULL);
  double delta = ((end.tv_sec  - start.tv_sec) * 1000000u + end.tv_usec - start.tv_usec) / 1.e6;
  cout << "serial time:" << delta << endl;
  
  gettimeofday(&start, NULL);
  //int rounds=5;
  //for (int i=0; i < rounds; i++) {
      T res2 = reduce(A, m);
      //t.next("parallel sum");
  //}
  cout << res2 << endl;
  gettimeofday(&end, NULL);
  delta = ((end.tv_sec  - start.tv_sec) * 1000000u + end.tv_usec - start.tv_usec) / 1.e6;
  cout << "parallel time:" << delta << endl;

//  cout << res1 << " " << res2 << endl;
}

