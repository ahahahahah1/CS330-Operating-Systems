#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define ul unsigned long

int main(int argc, char* argv[]) {
	ul num = strtoul(argv[argc - 1], NULL, 10);
	// printf("Input to square = %lu, argc = %d\n", num, argc);

	ul ans = num * num;
	// printf("Ans after square = %lu\n", ans);

	if(argc == 2) {
        printf("%lu\n", ans);
		return 0;
    }

	int n = argc - 1;
	char** args = (char **) malloc((n + 1) * sizeof(char *)); //ptrs are NULL by default so last arg will be NULL

	if(args == NULL) {
		printf("Unable to execute\n");
		return 1;
	}

	for(int i = 1; i < n; i++) {
		args[i - 1] = (char *)malloc((strlen(argv[i]) + 1) * sizeof(char));
		if (args[i - 1] == NULL) {
			printf("Unable to execute\n");
			return 1;
		}
		strcpy(args[i - 1], argv[i]);
	}

	args[n - 1] = (char *)malloc(25 * sizeof(char)); //max length of unsigned long is 20 decimal characters
	sprintf(args[n - 1], "%lu", ans);
	execv(args[0], args);

	// printf("copied arguments:\n");
	// for(int i = 0; i < argc; i++) {
	//     printf("%d %s\n", i, args[i]);
	// }
	return 0;
}
