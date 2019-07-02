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
static  int x[ARR_LEN*4];
static  int y[ARR_LEN*4];
static  int xx[ARR_LEN*4];
static  int yy[ARR_LEN*4];
static int grid[ARR_LEN] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};

void make_grid(int start, int qx, int qy)
{

/***** GRID 1 *****/
  int grid[ARR_LEN] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
  int add_x = 0;
  int add_y = 0;
  for(int i=0; i<ARR_LEN; i++){
    int col = grid[i] % BLOCK; 
    if (col == 0) {
      add_x = RANGE*3+(4);
    } else {
      add_x = RANGE*(col-1)+col;
    }
    int row = grid[i]/BLOCK; 
    if (col == 0) {
      add_y = RANGE*(row-1)+row;
    } else {
      add_y = RANGE*(row)+row;
    }
    int tx = 1+(rand()%(RANGE-1));
    int ty = 1+(rand()%(RANGE-1));
    x[i+start] = tx+add_x+qx;
    y[i+start] = ty+add_y+qy;

//    printf("%2d %3d %3d\t %3d %3d ax:%3d ay:%3d\n", i, x[i+start],y[i+start], tx, ty, add_x, add_y);
    printf("%d %d\n", x[i+start], y[i+start]);
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

void add_xy(int start, int add_x, int add_y)
{
  for(int i=0; i<ARR_LEN; i++){
    x[i+start] += add_x;
    y[i+start] += add_y;
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

void print_xy(void)
{
  for(int i=0; i<2; i++){ printf("%d %d\n", x[i], y[i]); }
  for(int i=0; i<2; i++){ printf("%d %d\n", x[i+(ARR_LEN*1)], y[i+ARR_LEN*1]); }
  for(int i=0; i<2; i++){ printf("%d %d\n", x[i+(ARR_LEN*2)], y[i+ARR_LEN*2]); }
  for(int i=0; i<2; i++){ printf("%d %d\n", x[i+(ARR_LEN*3)], y[i+ARR_LEN*3]); }

  for(int i=0; i<6; i++){ printf("%d %d\n", x[i+2], y[i]+2); }
  for(int i=0; i<6; i++){ printf("%d %d\n", x[i+(ARR_LEN*1)+2], y[i+ARR_LEN*1]+2); }
  for(int i=0; i<6; i++){ printf("%d %d\n", x[i+(ARR_LEN*2)+2], y[i+ARR_LEN*2]+2); }
  for(int i=0; i<6; i++){ printf("%d %d\n", x[i+(ARR_LEN*3)+2], y[i+ARR_LEN*3]+2); }

  for(int i=0; i<8; i++){ printf("%d %d\n", x[i+8], y[i]+8); }
  for(int i=0; i<8; i++){ printf("%d %d\n", x[i+(ARR_LEN*1)+8], y[i+ARR_LEN*1]+8); }
  for(int i=0; i<8; i++){ printf("%d %d\n", x[i+(ARR_LEN*2)+8], y[i+ARR_LEN*2]+8); }
  for(int i=0; i<8; i++){ printf("%d %d\n", x[i+(ARR_LEN*3)+8], y[i+ARR_LEN*3]+8); }

}

void print_xxyy(void)
{
  for(int i=0; i<ARR_LEN*4; i++){ printf("%d %d\n", xx[i], yy[i]); }
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
  int lower = 0, upper = 37;
  int index[ARR_LEN];
 
  srand(time(0)); 
  make_grid(0,0,0);
  make_grid(ARR_LEN,0,150);
  make_grid(ARR_LEN*2,150,0);
  make_grid(ARR_LEN*3,150,150);

/*
  for(int i=0; i<=3; i++){
    init_grid(index);
    shuffle(index, ARR_LEN);
    for(int j=0; j<ARR_LEN; j++){
      xx[j+(ARR_LEN*i)] = x[index[j]+(ARR_LEN*i)];
      yy[j+(ARR_LEN*i)] = y[index[j]+(ARR_LEN*i)];
    }
  }

  print_xxyy();
*/
} 
