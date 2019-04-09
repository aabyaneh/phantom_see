uint64_t* malloc(uint64_t size);

void swap(uint64_t* xp, uint64_t* yp) {
  uint64_t temp;

  temp = *xp;
  *xp = *yp;
  *yp = temp;
}

void selectionSort(uint64_t* arr, uint64_t n) {
  uint64_t i;
  uint64_t j;
  uint64_t min_idx;

  i = 0;
  while (i < n - 1) {
    min_idx = i;
    j = i + 1;
    while (j < n) {
      if (*(arr + j) < *(arr + min_idx))
        min_idx = j;
      j = j + 1;
    }

    swap(arr + min_idx, arr + i);

    i = i + 1;
  }
}

uint64_t main(uint64_t argc, uint64_t* argv) {
  uint64_t v1;
  uint64_t cnt;
  uint64_t* arr;

  cnt = 300;
  arr = malloc(cnt * 8);

  v1 = 0;
  while (v1 < cnt) {
    if (v1 != cnt/2)
      *(arr + v1) = cnt - v1;
    v1 = v1 + 1;
  }

  *(arr + cnt/2) = input(0, 2*cnt-1, 1);

  selectionSort(arr, cnt);

  return 0;
}