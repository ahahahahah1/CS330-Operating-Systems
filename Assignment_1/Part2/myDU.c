#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

struct Node {
    int data[2];
    struct Node *next;
};

struct Node* insert(struct Node *head, int* data) {
    struct Node *newNode = (struct Node*)malloc(sizeof(struct Node));
    if (newNode == NULL) {
        perror("malloc");
        exit(1);
    }
    newNode->data[0] = data[0];
    newNode->data[1] = data[1];
    newNode->next = head;
    return newNode;
}

int readSize(struct Node *head) {
    struct Node *current = head;
    unsigned long subdir_size = 0;
    char buf[32];
    read(current->data[0], buf, sizeof(buf));
    subdir_size = atoi(buf);
    return subdir_size;
}

struct Node* freeNode(struct Node *head) {
    struct Node *new_head = head->next;
    free(head);
    return new_head;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Unable to Execute");
        return 1;
    }

    struct Node* head = NULL;
    off_t calculated_size = 0;
    int fd[2];
    pid_t child_pid;

    char curr_dir[4096];
    char path[4500]; //curr_dir + file path which can be of size 260 bytes
    strcpy(curr_dir, argv[1]);

    DIR *dir = opendir(argv[1]);
    if (dir == NULL) {
        perror("opendir");
        return 1;
    }

    struct dirent *file;
    while ((file = readdir(dir)) != NULL) {
        if (strcmp(file->d_name, "..") != 0) {
            if(strcmp(file->d_name, ".") == 0) { //same directory. To be counted in that process.
                struct stat info;
                sprintf(path, "%s", curr_dir);
                if(stat(path, &info) == 0) {
                    calculated_size += info.st_size;
                    continue;
                }
                continue;
            }
            sprintf(path, "%s/%s", curr_dir, file->d_name);

            struct stat info;
            if (stat(path, &info) == 0) {
                // if (S_ISLNK(info.st_mode)) {
                //     printf("Symlink found\n");
                // }

                if (S_ISDIR(info.st_mode)) { //subdirectory. To be counted inside child process so 4096 is not added in parent
                    if(pipe(fd) == -1) {
                        perror("pipe");
                        exit(-1);
                    }
                    head = insert(head, fd);
                    
                    //creating a child process
                    child_pid = fork();
                    if(child_pid == -1) {
                        perror("fork");
                        exit(-1);
                    }
                    else if(!child_pid) { //child process
                        strcpy(curr_dir, path);
                        dir = opendir(path);
                        calculated_size = 0;
                        continue;
                    }
                    else {  //parent process, add the size of the subdirectory calculated by the child
                        wait(NULL);
                        calculated_size += readSize(head);
                        head = freeNode(head);
                    }
                } else if (S_ISREG(info.st_mode)) {
                    calculated_size += info.st_size;
                } else {
                    printf("Unable to Execute");
                    exit(-1);
                }
            } else {
                printf("Unable to Execute");
            }
        }
    }

    closedir(dir);

    if(head == NULL) {
        printf("%lu\n", calculated_size);
    }
    else {
        char val[20];
        sprintf(val, "%lu", calculated_size);
        // while(strlen(val) < 20) { //adding a padding since max possible length is 20
        //     strcat(val, "_");
        // }
        // printf("Final written value from child is %s\n", val);
        write(head->data[1], val, strlen(val));
    }
    return 0;
}