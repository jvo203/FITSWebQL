#include <atomic>
#include <stdio.h>

int main(int argc, char *argv[])
{
    int start_x = 0;
    int start_y = 0;
    int end_x = 5;
    int end_y = 5;

    std::atomic<bool> jmp = false;

    #pragma omp parallel
    {
#pragma omp single
      {
        for (auto idy = start_y; idy <= end_y; idy++)
        {
          if (jmp)
            break;

          for (auto idx = start_x; idx <= end_x; idx++)
          {
            if (jmp)
              break;

#pragma omp task
            {
                printf("idx: %d, idy: %d\n", idx, idy);
            }
          }
        }
      }
    }
}