#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
	char s[1000];
	FILE *fo;
	fo = fopen("result_size.csv", "w");
	//for (int size = 65000; size >= 20000; size -= 1000) {
	/*for (int size = 1000; size < 65535; size += 1000) {
		sprintf(s, "./client %s %s %d %d", argv[1], argv[2], size, 200);
		FILE *f = popen(s, "r");
		fgets(s, 1000, f);
		double latency;
		sscanf(strchr(s, '=') + 2, "%lf", &latency);
		fprintf(fo, "%d,%f\n", size, latency);
		pclose(f);
	}
	fclose(fo);*/
	//return 0;

	fo = fopen("result_cnt.csv", "w");
	for (int count = 10; count < 1000; count += count < 200 ? 10:100) {
		sprintf(s, "./client %s %s %d %d", argv[1], argv[2], 50000, count);
		FILE *f = popen(s, "r");
		fgets(s, 1000, f);
		double latency;
		sscanf(strchr(s, '=') + 2, "%lf", &latency);
		fprintf(fo, "%d,%f\n", count, latency);
		pclose(f);
	}
	fclose(fo);

	return 0;
}