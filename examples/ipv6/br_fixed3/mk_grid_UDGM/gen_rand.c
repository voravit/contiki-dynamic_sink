#include <stdio.h> 
#include <stdlib.h> 
#include <time.h> 
  
// Generates and prints 'count' random 
// numbers in range [lower, upper]. 
void printRandoms(int lower, int upper, int count) 
{ 
    int i; 
    int x[count];
    int y[count];
    for (i = 0; i < count; i++) { 
        x[i] = (rand() % (upper - lower + 1)) + lower + 150; 
    } 
    for (i = 0; i < count; i++) { 
        y[i] = (rand() % (upper - lower + 1)) + lower + 150; 
    } 
    for (i = 0; i < count; i++) { 
        printf("%d %d\n", x[i],y[i]); 
    } 
} 
  
// Driver code 
int main() 
{ 
    int lower = 0, upper = 150, count = 100; 
  
    // Use current time as  
    // seed for random generator 
    srand(time(0)); 
  
    printRandoms(lower, upper, count); 
  
    return 0; 
} 
