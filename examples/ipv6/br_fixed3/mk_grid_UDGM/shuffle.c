#include <stdio.h> 
#include <stdlib.h> 
#include <time.h> 

#define COUNT 10 
#define ARR_LEN 16
#define RANGE 37
#define BLOCK 4
/*
static int grid1[ARR_LEN] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static int grid2[ARR_LEN] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static int grid3[ARR_LEN] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static int grid4[ARR_LEN] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
*/
static int grid[ARR_LEN] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};

void shuffle(int *array, size_t n)
{
    if (n > 1) 
    {
        size_t i;
        for (i = 0; i < n - 1; i++) 
        {
          size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
          int t = array[j];
          array[j] = array[i];
          array[i] = t;
        }
    }
}

void init_grid(int *grid)
{
  for(int i=0; i<ARR_LEN; i++){
    grid[i] = i+1;
  }
}

void print_grid(int *grid)
{
  for(int i=0; i<ARR_LEN; i++){
    printf("%d ",grid[i]); 
  }
  printf("\n");
}

void print_index(int *array, int offset)
{
  for(int i=0; i<ARR_LEN; i++){
    printf("%d\n",array[i]+offset); 
  }
  //printf("\n");
}

int main() 
{ 
  int index[ARR_LEN];
  srand(time(0)); 

  init_grid(index);
  shuffle(index, 16);
  print_index(index, 0);

//printf("\n");
  init_grid(index);
  shuffle(index, 16);
  print_index(index, ARR_LEN);

  init_grid(index);
  shuffle(index, 16);
  print_index(index, ARR_LEN*2);

  init_grid(index);
  shuffle(index, 16);
  print_index(index, ARR_LEN*3);

} 
