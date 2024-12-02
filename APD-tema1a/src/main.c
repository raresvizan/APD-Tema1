#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	char word[50];
	int id_file;
} WordEntry;

typedef struct {
	int thread_id;
	char **files;
	int n_files;
	pthread_mutex_t *mutex;
	WordEntry **partial_words;
	int *partial_count;
	int *next_file_id;
} ThreadArgs;

void normalize_word(char *word)
{
	for (int i = 0; word[i]; i++) {
		word[i] = tolower(word[i]);
		if (!isalpha(word[i])) {
			for (int j = i; word[j]; j++) {
				word[j] = word[j + 1];
			}
			i--;
		}
	}
}

void *map_function(void *arg)
{
	ThreadArgs *targs = (ThreadArgs *)arg;
	WordEntry *curr_words = malloc(1000 * sizeof(WordEntry));
	int curr_count = 0;

	while (1) {
		pthread_mutex_lock(targs->mutex);
		if (*(targs->next_file_id) >= targs->n_files) {
			pthread_mutex_unlock(targs->mutex);
			break;
		}
        int file_id = *(targs->next_file_id);
		(*(targs->next_file_id))++;
		pthread_mutex_unlock(targs->mutex);

		FILE *file = fopen(targs->files[file_id], "r");
		if (file == NULL) {
			perror("Error opening file");
			exit(1);
		}

		char word[50];
		while (fscanf(file, "%s", word) == 1) {
			normalize_word(word);
			int found = 0;
			for (int i = 0; i < curr_count; i++) {
				if (strcmp(curr_words[i].word, word) == 0 && curr_words[i].id_file == file_id) {
					found = 1;
					break;
				}
			}
			if (!found) {
				strcpy(curr_words[curr_count].word, word);
				curr_words[curr_count].id_file = file_id;
				curr_count++;
			}
		}
		fclose(file);
	}

	pthread_mutex_lock(targs->mutex);
	targs->partial_words[targs->thread_id] = curr_words;
	targs->partial_count[targs->thread_id] = curr_count;
	pthread_mutex_unlock(targs->mutex);

	pthread_exit(NULL);
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		printf("Too few arguments\n");
		exit(1);
	}
	int n_tmap = atoi(argv[1]);
	FILE *file_list = fopen(argv[2], "r");
	int n_files;
	fscanf(file_list, "%d", &n_files);
	char **files = malloc(n_files * sizeof(char *));
	for (int i = 0; i < n_files; i++) {
		files[i] = malloc(50 * sizeof(char));
		fscanf(file_list, "%s", files[i]);
	}
	fclose(file_list);

	pthread_t threds[n_tmap];
	ThreadArgs targs[n_tmap];
	pthread_mutex_t mutex;
	pthread_mutex_init(&mutex, NULL);

	WordEntry **partial_words = malloc(n_tmap * sizeof(WordEntry *));
	int *partial_count = malloc(n_tmap * sizeof(int));
    int next_file_id = 0;

    for (int i = 0; i < n_tmap; i++) {
        targs[i].thread_id = i;
        targs[i].files = files;
        targs[i].n_files = n_files;
        targs[i].mutex = &mutex;
        targs[i].partial_words = partial_words;
        targs[i].partial_count = partial_count;
        targs[i].next_file_id = &next_file_id;

        pthread_create(&threds[i], NULL, map_function, &targs[i]);
    }

    for (int i = 0; i < n_tmap; i++) {
        pthread_join(threds[i], NULL);
    }

    for (int i = 0; i < n_tmap; i++) {
        printf("Thread %d processed %d words\n", i, partial_count[i]);
        // show processed words
        for (int j = 0; j < partial_count[i]; j++) {
            printf("%s %d\n", partial_words[i][j].word, partial_words[i][j].id_file);
        }
    }

    for (int i = 0; i < n_tmap; i++) {
        free(partial_words[i]);
    }
    free(partial_words);
    free(partial_count);
    pthread_mutex_destroy(&mutex);

	return 0;
}