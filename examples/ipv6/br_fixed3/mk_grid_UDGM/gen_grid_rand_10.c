#include <stdio.h> 
#include <stdlib.h> 
#include <time.h> 

#define COUNT 10 
// Generates and prints 'count' random 
// numbers in range [lower, upper]. 
void printRandoms(int lower, int upper, int count) 
{ 
    int i; 
    int x[count];
    int y[count];
    for (i = 0; i < count; i++) { 
        x[i] = (rand() % (upper - lower + 1)) + lower; 
    } 
    for (i = 0; i < count; i++) { 
        y[i] = (rand() % (upper - lower + 1)) + lower; 
    } 
    for (i = 0; i < count; i++) { 
        printf("%d %d\n", x[i],y[i]); 
    } 

} 

void make_grid(int *grid, int qx, int qy)
{
  for(int i=0; i<10; i++){
    int add_x = 0;
    int add_y = 0;

    int col = grid[i] % 5; 
    if (col == 0) {
      add_x = 30*4;
    } else {
      add_x = 30*(col-1);
    }

    int row = grid[i]/5; 
    if (col == 0) {
      add_y = 30*(row-1);
    } else {
      add_y = 30*(row);
    }

    int x = rand() % 31;
    int y = rand() % 31;

    printf("%d %d\n", x+add_x+qx, y+add_y+qy);

  }
}

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

void print_grid(int *grid)
{
  for(int i=0; i<25; i++){
    printf("%d ",grid[i]); 
  }
  printf("\n");
}

// Driver code 
int main() 
{ 
/*
    int lower = 0, upper = 150, count = 100; 
    // Use current time as  
    // seed for random generator 
    srand(time(0)); 
    printRandoms(lower, upper, count); 
    return 0; 
*/
  int lower = 0, upper = 30;
  srand(time(0)); 
  int grid[25] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25};
  //print_grid(grid);
  shuffle(grid, 25);
  //print_grid(grid);
  make_grid(grid,0,0);
  shuffle(grid, 25);
  //print_grid(grid);
  make_grid(grid,0,150);
  shuffle(grid, 25);
  //print_grid(grid);
  make_grid(grid,150,0);
  shuffle(grid, 25);
  //print_grid(grid);
  make_grid(grid,150,150);
} 
