#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdatomic.h>

#define MAX_PATH 4096
#define NUM_THREADS 16  

typedef struct FileNode {
    char *filename;
    struct FileNode *next;
} FileNode;

FileNode *head = NULL, *tail = NULL;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

atomic_int files_processed = 0;
int total_files = 0;

// Simple cache for created directories
typedef struct DirNode {
    char path[MAX_PATH];
    struct DirNode *next;
} DirNode;

DirNode *dir_cache = NULL;
pthread_mutex_t dir_mutex = PTHREAD_MUTEX_INITIALIZER;

int dir_in_cache(const char *path) {
    DirNode *n = dir_cache;
    while (n) {
        if (strcmp(n->path, path) == 0) return 1;
        n = n->next;
    }
    return 0;
}

void cache_mkdir(const char *path) {
    pthread_mutex_lock(&dir_mutex);
    if (!dir_in_cache(path)) {
        mkdir(path, 0755);
        DirNode *new = malloc(sizeof(DirNode));
        strncpy(new->path, path, MAX_PATH);
        new->next = dir_cache;
        dir_cache = new;
    }
    pthread_mutex_unlock(&dir_mutex);
}

void enqueue(const char *filename) {
    FileNode *node = malloc(sizeof(FileNode));
    node->filename = strdup(filename);
    node->next = NULL;

    pthread_mutex_lock(&queue_mutex);
    if (!tail) head = tail = node;
    else {
        tail->next = node;
        tail = node;
    }
    pthread_mutex_unlock(&queue_mutex);
}

char *dequeue() {
    pthread_mutex_lock(&queue_mutex);
    if (!head) {
        pthread_mutex_unlock(&queue_mutex);
        return NULL;
    }
    FileNode *node = head;
    head = node->next;
    if (!head) tail = NULL;
    pthread_mutex_unlock(&queue_mutex);

    char *res = node->filename;
    free(node);
    return res;
}

void progress_bar(int current, int total) {
    int width = 40;
    float ratio = (float)current / total;
    int bars = (int)(ratio * width);

    printf("\r[");
    for (int i = 0; i < bars; i++) printf("█");
    for (int i = bars; i < width; i++) printf("-");
    printf("] %d/%d (%.2f%%)", current, total, ratio * 100);
    fflush(stdout);
}

void *worker(void *arg) {
    while (1) {
        char *file = dequeue();
        if (!file) break;

        // Parse file name into z, x, y
        char z[16], x[16], y[16];
        if (sscanf(file, "%15[^-]-%15[^-]-%15[^.].png", z, x, y) != 3) {
            free(file);
            atomic_fetch_add(&files_processed, 1);
            continue;
        }

        char dir1[MAX_PATH], dir2[MAX_PATH], outpath[MAX_PATH];
        snprintf(dir1, MAX_PATH, "%s", z);
        snprintf(dir2, MAX_PATH, "%s/%s", z, x);
        snprintf(outpath, MAX_PATH, "%s/%s.png", dir2, y);

        cache_mkdir(dir1);
        cache_mkdir(dir2);

        rename(file, outpath);

        atomic_fetch_add(&files_processed, 1);
        progress_bar(atomic_load(&files_processed), total_files);
        free(file);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s /path/to/tiles\n", argv[0]);
        return 1;
    }

    chdir(argv[1]);

    DIR *d = opendir(".");
    if (!d) {
        perror("opendir");
        return 1;
    }

    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_type == DT_REG && strstr(e->d_name, ".png")) {
            enqueue(e->d_name);
            total_files++;
        }
    }
    closedir(d);

    printf("Found %d files\n", total_files);

    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_create(&threads[i], NULL, worker, NULL);
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    printf("\nDone.\n");
    return 0;
}
