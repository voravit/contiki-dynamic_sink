#include <stdio.h> 
#include <stdlib.h> 
#include <time.h> 

#define COUNT 10 

static int grid1[25] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25};
static int grid2[25] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25};
static int grid3[25] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25};
static int grid4[25] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25};

void make_grid(void)
{
  int x[100];
  int y[100];

/***** GRID 1 *****/
  int add_x = 0;
  int add_y = 0;
  for(int i=0; i<25; i++){
    int col = grid1[i] % 5; 
    if (col == 0) {
      add_x = 30*4;
    } else {
      add_x = 30*(col-1);
    }
    int row = grid1[i]/5; 
    if (col == 0) {
      add_y = 30*(row-1);
    } else {
      add_y = 30*(row);
    }
    int tx = 1+(rand()%30);
    int ty = 1+(rand()%30);
    x[i] = tx+add_x;
    y[i] = ty+add_y;
  }

  for(int i=0; i<25; i++){
    int col = grid2[i] % 5; 
    if (col == 0) {
      add_x = 30*4;
    } else {
      add_x = 30*(col-1);
    }
    int row = grid2[i]/5; 
    if (col == 0) {
      add_y = 30*(row-1);
    } else {
      add_y = 30*(row);
    }
    int tx = 1+(rand()%30);
    int ty = 1+(rand()%30);
    x[i+25] = tx+add_x;
    y[i+25] = ty+add_y+150;
  }

  for(int i=0; i<25; i++){
    int col = grid3[i] % 5; 
    if (col == 0) {
      add_x = 30*4;
    } else {
      add_x = 30*(col-1);
    }
    int row = grid3[i]/5; 
    if (col == 0) {
      add_y = 30*(row-1);
    } else {
      add_y = 30*(row);
    }
    int tx = 1+(rand()%30);
    int ty = 1+(rand()%30);
    x[i+50] = tx+add_x+150;
    y[i+50] = ty+add_y;
  }

  for(int i=0; i<25; i++){
    int col = grid4[i] % 5; 
    if (col == 0) {
      add_x = 30*4;
    } else {
      add_x = 30*(col-1);
    }
    int row = grid4[i]/5; 
    if (col == 0) {
      add_y = 30*(row-1);
    } else {
      add_y = 30*(row);
    }
    int tx = 1+(rand()%30);
    int ty = 1+(rand()%30);
    x[i+75] = tx+add_x+150;
    y[i+75] = ty+add_y+150;
  }
/*
  for(int i=0; i<10; i++){ printf("%d %d\n", x[i], y[i]); }
  for(int i=0; i<10; i++){ printf("%d %d\n", x[i+25], y[i+25]); }
  for(int i=0; i<10; i++){ printf("%d %d\n", x[i+50], y[i+50]); }
  for(int i=0; i<10; i++){ printf("%d %d\n", x[i+75], y[i+75]); }

  for(int i=0; i<10; i++){ printf("%d %d\n", x[i+10], y[i+10]); }
  for(int i=0; i<10; i++){ printf("%d %d\n", x[i+35], y[i+35]); }
  for(int i=0; i<10; i++){ printf("%d %d\n", x[i+60], y[i+60]); }
  for(int i=0; i<10; i++){ printf("%d %d\n", x[i+85], y[i+85]); }

  for(int i=0; i<5; i++){ printf("%d %d\n", x[i+20], y[i+20]); }
  for(int i=0; i<5; i++){ printf("%d %d\n", x[i+45], y[i+45]); }
  for(int i=0; i<5; i++){ printf("%d %d\n", x[i+70], y[i+70]); }
  for(int i=0; i<5; i++){ printf("%d %d\n", x[i+95], y[i+95]); }
*/
  for(int i=0; i<100; i++){ printf("%d %d\n", x[i], y[i]); }
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
  shuffle(grid1, 25);
  shuffle(grid2, 25);
  shuffle(grid3, 25);
  shuffle(grid4, 25);
  print_grid(grid1);
  print_grid(grid2);
  print_grid(grid3);
  print_grid(grid4);

  make_grid();
/*
  make_grid(grid1,0,0);
  make_grid(grid2,0,150);
  make_grid(grid3,150,0);
  make_grid(grid4,150,150);
*/
} 
