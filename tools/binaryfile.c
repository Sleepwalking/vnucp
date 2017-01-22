#include <stdio.h>
#include <stdlib.h>

int main(){

	FILE * binfile = fopen("test.bin", "wb");



	for(int i = 0; i < 1024; i++){
		char char_lol = rand() % 2;
		fwrite(& char_lol, 1, 1, binfile);
	}

	fclose(binfile);

}
