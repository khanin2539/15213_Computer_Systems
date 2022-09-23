/* 
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */ 
// Name: Khanin Udomchoksakul
//ID: Kudomcho
#include <stdio.h>
#include "cachelab.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/* 
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded. 
 */

//reference: https://www.cs.cmu.edu/afs/cs/academic/class/15213-s16/www/recitations/recitation07-cachelab.pdf
void for32_32(int M, int N, int A[N][M], int B[M][N]){

    int row, col, rowBlock, rowCol;  // row, col stands for index at element level
                         // i1, j1 stands for index of level1 block start
    for (row = 0; row < N; row += 8) { // next block
        for (col = 0; col < M; col += 8) { // next block
            if (row != col) {  // Non-diagonal block guarantees no eviction problem
                 for (rowBlock = row; rowBlock < row + 8; rowBlock++) { // next index of each block
                    for (rowCol = col; rowCol < col + 8; rowCol++) { // next index of each block
                        B[rowBlock][rowCol] = A[rowCol][rowBlock];
                    }
                }
            
            } 
            else {  
                // now we have a diagonal block
                // due to that the 1st row of B evicts the 1st row of A
                 for (rowBlock = row; rowBlock < row + 8; rowBlock++) {
                    for (rowCol = col; rowCol < col + 8; rowCol++) {
                        if (rowBlock != rowCol) 
                         B[rowCol][rowBlock] = A[rowBlock][rowCol];
                    }

                    // prevent eviction
                    B[rowBlock][rowBlock] = A[rowBlock][rowBlock];
                }
            }
        }
    }


}

//reference: https://www.cs.cmu.edu/afs/cs/academic/class/15213-s16/www/recitations/recitation07-cachelab.pdf
void for64_64(int M, int N, int A[N][M], int B[M][N]){

        int row, col, h, v; //h->horizontal, v-> vertical
		int f, s, t, fo, fif, si, sev, eig;
		for (row = 0; row < N; row += 8){ // each 8 byte block
			for (col = 0; col< M; col += 8) // each 8 byte block
			{
                // for each element of first 4 byte on block
				for (h = row; h < row + 4; h++){ //row = 0
					f = A[h][col]; //x = 0, j = 0  A[0][0] A[1][0] A[2][0] A[3][0] A[][1]
                    s = A[h][col+1]; //x = 0, j = 1 A[0][1]
                    t = A[h][col+2]; 
                    fo = A[h][col+3];
					fif = A[h][col+4]; 
                    si = A[h][col+5]; 
                    sev = A[h][col+6]; 
                    eig = A[h][col+7]; //A[0][7]
					
					B[col][h] = f; //B[0][0]
                    B[col+1][h] = s; //B[1][0]
                    B[col+2][h] = t; 
                    B[col+3][h] = fo;
					B[col][h+4] = fif; 
                    B[col+1][h+4] = si; 
                    B[col+2][h+4] = sev; 
                    B[col+3][h+4] = eig; //B[7][0]
				}
				for (v = col; v < col + 4; ++v)
				{
					f = A[row+4][v]; //A[4][0]
                    s = A[row+5][v]; //A[5][0]
                    t = A[row+6][v]; //A[6][0]
                    fo = A[row+7][v]; //A[7][0]
					fif = B[v][row+4]; //B[0][4]
                    si = B[v][row+5]; //B[0][5]
                    sev = B[v][row+6]; //B[0][6]
                    eig = B[v][row+7]; //B[0][7]
					
					B[v][row+4] = f; //B[0][4]
                    B[v][row+5] = s; //B[0][5]
                    B[v][row+6] = t; //B[0][6]
                    
                    B[v][row+7] = fo; //B[0][7]
					B[v+4][row] = fif; //B[4][0]
                    B[v+4][row+1] = si; //B[4][1]
                    B[v+4][row+2] = sev; //B[4][2]
                    B[v+4][row+3] = eig; //B[4][3]
				}
				for (h = row + 4; h < row + 8; ++h)
				{
					f = A[h][col+4]; 
                    s = A[h][col+5]; 
                    t = A[h][col+6]; 
                    fo = A[h][col+7];
					B[col+4][h] = f; 
                    B[col+5][h] = s; 
                    B[col+6][h] = t; 
                    B[col+7][h] = fo;
				}
			}
        } 
	}

//reference: https://www.cs.cmu.edu/afs/cs/academic/class/15213-s16/www/recitations/recitation07-cachelab.pdf

void for_61_67(int M, int N, int A[N][M], int B[M][N]) {
    int row, col, eleh, elev; // row and column of the block, ele on horizontal and element on vertical block 
    int block_size = 16; // assign block size =16
    for (row = 0; row < N; row += block_size) { // keep going on each block
        for (col = 0; col < M; col += block_size) { // keep going on each block
            for (eleh = row; eleh < row + block_size && eleh < N; eleh++) { // moving from each element within the block st. no more than k<N and row+ block size
                for (elev = col; elev < col + block_size && elev < M; elev++) { // moving from each element within the block st. no more than k<N and row+ block size
                    B[elev][eleh] = A[eleh][elev]; 
                }
            }
        }
    }
}

char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
    if(M==32) {
        for32_32(M, N, A, B);
    }
    else if(M==64){
        for64_64(M, N, A, B);
    }
    else if(M==61){
        for_61_67(M, N, A, B);
    }
}

/* 
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started. 
 */ 

/* 
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, tmp;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; j++) {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }    

}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc); 

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc); 

}

/* 
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}

