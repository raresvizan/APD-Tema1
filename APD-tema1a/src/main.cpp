#include <algorithm>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

typedef struct {
	int thread_id;
	vector<string> *files;
	int n_files;
	pthread_mutex_t *mutex;
	vector<unordered_map<string, set<int>>> *partial_words;
	int *next_file_id;
	pthread_barrier_t *barrier;
	int n_tmap;
	char *next_letter;
} ThreadArgs;

void normalize_word(string &word)
{
	// convert all chr to lowercase and remove non-alphabetic chr
	for (int i = 0; i < word.size(); i++) {
		if (word[i] >= 'A' && word[i] <= 'Z') {
			word[i] = word[i] + 32;
		}
		if (!(word[i] >= 'a' && word[i] <= 'z')) {
			word.erase(i, 1);
			i--;
		}
	}
}

void *mapper_function(ThreadArgs *targs)
{
	unordered_map<string, set<int>> curr_words;
	while (1) {
		// get next file to process
		pthread_mutex_lock(targs->mutex);
		if (*(targs->next_file_id) >= targs->n_files) {
			pthread_mutex_unlock(targs->mutex);
			break;
		}
		int file_id = *(targs->next_file_id);
		(*(targs->next_file_id))++;
		pthread_mutex_unlock(targs->mutex);

		// read words from the current file and normalize them
		ifstream file((*targs->files)[file_id]);
		string word;
		while (file >> word) {
			normalize_word(word);
			if (!word.empty()) {
				curr_words[word].insert(file_id + 1);
			}
		}
		file.close();
	}

	// save the words in the threads partial words
	pthread_mutex_lock(targs->mutex);
	(*targs->partial_words)[targs->thread_id] = curr_words;
	pthread_mutex_unlock(targs->mutex);
}

void *reduce_function(ThreadArgs *targs)
{
	while (1) {
		// get next letter to process
		pthread_mutex_lock(targs->mutex);
		if (*(targs->next_letter) > 'z') {
			pthread_mutex_unlock(targs->mutex);
			break;
		}
		char letter = *(targs->next_letter);
		(*(targs->next_letter))++;

		// get all words starting with the current letter from threads partial results
		unordered_map<string, set<int>> curr_words;
		for (int i = 0; i < targs->n_tmap; i++) {
			auto entry = (*targs->partial_words)[i].begin();
			while (entry != (*targs->partial_words)[i].end()) {
				if (entry->first[0] == letter) {
					curr_words[entry->first].insert(entry->second.begin(), entry->second.end());
					(*targs->partial_words)[i].erase(entry++);
				} else {
					entry++;
				}
			}
		}
		pthread_mutex_unlock(targs->mutex);

		// sort the current words by number of files or alphabetically if the number of files is equal
		vector<pair<string, set<int>>> sorted_words(curr_words.begin(), curr_words.end());
		sort(sorted_words.begin(), sorted_words.end(), [](pair<string, set<int>> &a, pair<string, set<int>> &b) {
			if (a.second.size() == b.second.size()) {
				return a.first < b.first;
			}
			return a.second.size() > b.second.size();
		});

		// write the current words to the output files
		string file_name = string(1, letter) + ".txt";
		ofstream out_file(file_name);

		for (auto &entry : sorted_words) {
			out_file << entry.first << ":[";
			for (auto &file_id : entry.second) {
				out_file << file_id;
				if (file_id != *entry.second.rbegin()) {
					out_file << " ";
				}
			}
			out_file << "]" << endl;
		}
		out_file.close();
	}
}

void *thread_function(void *arg)
{
	ThreadArgs *targs = (ThreadArgs *)arg;
	if (targs->thread_id < targs->n_tmap) {
		mapper_function(targs);
	}

	pthread_barrier_wait(targs->barrier);

	if (targs->thread_id >= targs->n_tmap) {
		reduce_function(targs);
	}

	pthread_exit(NULL);
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		printf("Too few arguments\n");
		exit(1);
	}

	// read arguments and file names
	int n_tmap = atoi(argv[1]);
	int n_tred = atoi(argv[2]);
	ifstream file_list(argv[3]);
	int n_files;
	file_list >> n_files;
	vector<string> files(n_files);
	for (int i = 0; i < n_files; i++) {
		file_list >> files[i];
	}
	file_list.close();

	// create threads, thread arguments, mutex and barrier
	pthread_t threads[n_tmap + n_tred];
	ThreadArgs targs[n_tmap + n_tred];
	pthread_mutex_t mutex;
	pthread_mutex_init(&mutex, NULL);
	pthread_barrier_t barrier;
	pthread_barrier_init(&barrier, NULL, n_tmap + n_tred);
	vector<unordered_map<string, set<int>>> partial_words(n_tmap);
	int next_file_id = 0;
	char next_letter = 'a';

	// start threads
	for (int i = 0; i < n_tmap + n_tred; i++) {
		targs[i].thread_id = i;
		targs[i].files = &files;
		targs[i].n_files = n_files;
		targs[i].mutex = &mutex;
		targs[i].barrier = &barrier;
		targs[i].partial_words = &partial_words;
		targs[i].next_file_id = &next_file_id;
		targs[i].n_tmap = n_tmap;
		targs[i].next_letter = &next_letter;

		pthread_create(&threads[i], NULL, thread_function, &targs[i]);
	}

	// wait for threads to finish
	for (int i = 0; i < n_tmap + n_tred; i++) {
		pthread_join(threads[i], NULL);
	}

	// destroy mutex and barrier
	pthread_mutex_destroy(&mutex);
	pthread_barrier_destroy(&barrier);

	return 0;
}