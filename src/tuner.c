/*
 * Marvin - an UCI/XBoard compatible chess engine
 * Copyright (C) 2015 Martin Danielsson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>

#include "chess.h"
#include "config.h"
#include "bitboard.h"
#include "debug.h"
#include "fen.h"
#include "search.h"
#include "board.h"
#include "hash.h"
#include "tuningparam.h"
#include "evalparams.h"
#include "eval.h"

/* Files that the tuning result is written to */
#define TUNING_FINAL_RESULT_FILE "tuning.final"
#define TUNING_ITERATION_RESULT_FILE "tuning.iter%d"

/* The approximate length of an EPD string in the trainingset */
#define APPROX_EPD_LENGTH 60

/* The tuning constant K */
#define K 1.090

/* Constants used when calculating K */
#define K_MIN 0.00
#define K_MAX 2.0
#define K_STEP 0.001

/* Training position */
struct trainingpos {
    char    *epd;
    double  result;
};

/* Training set */
struct trainingset {
    struct trainingpos *positions;
    int                size;
};

/* Tuning set */
struct tuningset {
    struct tuning_param *params;
    int                 size;
    int                 nactive;
};

/* Different state for worker threads */
enum worker_state {
    WORKER_IDLE,
    WORKER_RUNNING,
    WORKER_FINISHED,
    WORKER_STOPPED
};

/* Worker thread */
struct worker {
    pthread_t           thread;
    struct trainingset  *trainingset;
    int                 first_pos;
    int                 last_pos;
    double              k;
    double              sum;
    enum worker_state   state;
};

/* Mutexes and conditions used for syncronization of workers */
static pthread_mutex_t worker_start_lock;
static pthread_mutex_t worker_finished_lock;
static pthread_cond_t workers_start_cond;
static pthread_cond_t workers_finished_cond;

/* Workers used for calculating errors */
struct worker   *workers = NULL;
static int      nworkerthreads = 0;

static double calc_texel_sigmoid(int score, double k)
{
    double exp;

    exp = -(k*((double)score)/400.0);
    return 1.0/(1.0 + pow(10.0, exp));
}

static void* calc_texel_error_func(void *data)
{
    struct worker       *worker;
    struct gamestate    *pos;
    struct trainingset  *trainingset;
    int                 iter;
    int                 score;
    double              sigmoid;
    double              result;

    /* Initialize worker */
    worker = (struct worker*)data;
    pos = create_game_state(DEFAULT_MAIN_HASH_SIZE);
    hash_tt_destroy_table(pos);
    hash_pawntt_destroy_table(pos);
    trainingset = worker->trainingset;

    /* Worker main loop */
    while (true) {
        /* Wait for signal to start */
        pthread_mutex_lock(&worker_start_lock);
        while ((worker->state != WORKER_RUNNING) &&
               (worker->state != WORKER_STOPPED)) {
            pthread_cond_wait(&workers_start_cond, &worker_start_lock);
        }
        pthread_mutex_unlock(&worker_start_lock);
        if (worker->state == WORKER_STOPPED) {
            break;
        }

        /* Iterate over all training positions assigned to this worker */
        worker->sum = 0.0;
        for (iter=worker->first_pos;iter<=worker->last_pos;iter++) {
            /* Setup position */
            board_reset(pos);
            (void)fen_setup_board(pos, trainingset->positions[iter].epd, true);

            /* Do a quiscence search to get a score for the position */
            search_reset_data(pos);
            pos->tc_type = TC_INFINITE;
            pos->sd = 0;
            pos->silent = true;
            pos->use_tablebases = false;
            score = search_get_quiscence_score(pos);
            score = (pos->stm == WHITE)?score:-score;

            /* Calculate error */
            sigmoid = calc_texel_sigmoid(score, worker->k);
            result = trainingset->positions[iter].result;
            worker->sum += ((result - sigmoid)*(result - sigmoid));
        }

        /* Signal that this iteration is finished */
        pthread_mutex_lock(&worker_finished_lock);
        worker->state = WORKER_FINISHED;
        pthread_cond_signal(&workers_finished_cond);
        pthread_mutex_unlock(&worker_finished_lock);
    }

    return NULL;
}

static double calc_texel_error(struct trainingset *trainingset, double k)
{
    int     iter;
    int     nfinished;
    double  sum;

    /* Start all worker threads */
    pthread_mutex_lock(&worker_start_lock);
    for (iter=0;iter<nworkerthreads;iter++) {
        workers[iter].k = k;
        workers[iter].state = WORKER_RUNNING;
    }
    pthread_cond_broadcast(&workers_start_cond);
    pthread_mutex_unlock(&worker_start_lock);

    /* Wait for all workers to finish */
    pthread_mutex_lock(&worker_finished_lock);
    nfinished = 0;
    while (nfinished < nworkerthreads) {
        nfinished = 0;
        for (iter=0;iter<nworkerthreads;iter++) {
            if (workers[iter].state == WORKER_FINISHED) {
                nfinished++;
            }
        }
        if (nfinished < nworkerthreads) {
            pthread_cond_wait(&workers_finished_cond, &worker_finished_lock);
        }
    }
    pthread_mutex_unlock(&worker_finished_lock);

    /* Summarize the result of all workers and calculate the error */
    sum = 0.0;
    for (iter=0;iter<nworkerthreads;iter++) {
        sum += workers[iter].sum;
    }

    return sum/(double)trainingset->size;
}

static void local_optimize(struct tuningset *tuningset,
                           struct trainingset *trainingset, int stepsize)
{
    struct  tuning_param *param;
    double  best_e;
    double  e;
    bool    improved;
    bool    improved_local;
    int     pi;
    int     niterations;
    bool    undo;
    FILE    *fp;
    char    path[64];
    int     count;
    int     delta;

    /* Calculate current error */
    tuning_param_assign_current(tuningset->params);
    best_e = calc_texel_error(trainingset, K);
    printf("Initial error: %f\n", best_e);

    /* Loop until no more improvements are found */
    delta = stepsize;
    niterations = 0;
    improved = true;
    while (improved || (niterations <= 1)) {
        /* Loop over all parameters in the tuning set */
        improved = false;
        count = 0;
        for (pi=0;pi<tuningset->size;pi++) {
            /* Get the tuning parameter */
            param = &tuningset->params[pi];
            if (!param->active) {
                continue;
            }
            count++;
            printf("\r%d/%d", count, tuningset->nactive);

            /*
             * Start by increasing the value of the parameter in steps
             * of one. Continue increasing until no more improvement is
             * found.
             */
            undo = false;
            improved_local = false;
            while ((param->current+delta) <= param->max) {
                param->current += delta;
                tuning_param_assign_current(tuningset->params);
                e = calc_texel_error(trainingset, K);
                if (e < best_e) {
                    best_e = e;
                    improved = true;
                    improved_local = true;
                } else {
                    undo = true;
                    break;
                }
            }
            if (undo) {
                param->current -= delta;
                tuning_param_assign_current(tuningset->params);
            }

            /* If no improvement was found try decreasing the value instead */
            if (!improved_local) {
                undo = false;
                improved_local = false;
                while ((param->current-delta) >= param->min) {
                    param->current -= delta;
                    tuning_param_assign_current(tuningset->params);
                    e = calc_texel_error(trainingset, K);
                    if (e < best_e) {
                        best_e = e;
                        improved = true;
                    } else {
                        undo = true;
                        break;
                    }
                }
                if (undo) {
                    param->current += delta;
                    tuning_param_assign_current(tuningset->params);
                }
            }

            /* Make sure parameters are up to date */
            tuning_param_assign_current(tuningset->params);
        }

        /* Set the step size to 1 for subsequent iterations */
        delta = 1;

        /* Output the result after the current iteration */
        niterations++;
        printf("\rIteration %d complete, error %f\n", niterations, best_e);
        sprintf(path, TUNING_ITERATION_RESULT_FILE, niterations);
        fp = fopen(path, "w");
        if (fp != NULL) {
            tuning_param_write_parameters(fp, tuningset->params, true);
            fclose(fp);
        }
    }

    printf("Final error: %f\n", best_e);
}

static void free_trainingset(struct trainingset *trainingset)
{
    int k;

    if ((trainingset == NULL) || (trainingset->positions == NULL)) {
        return;
    }

    for (k=0;k<trainingset->size;k++) {
        free(trainingset->positions[k].epd);
    }
    free(trainingset->positions);
    free(trainingset);
}

static struct trainingset* read_trainingset(struct gamestate *pos, char *file)
{
    struct             stat sb;
    struct trainingset *trainingset;
    int                ntot;
    FILE               *fp;
    char               buffer[512];
    char               *str;

    /* Get the size of the file */
    if (stat(file, &sb) < 0) {
        return NULL;
    }

    /* Allocate an array to hold the training positions */
    trainingset = malloc(sizeof(trainingset));
    ntot = (int)sb.st_size/APPROX_EPD_LENGTH;
    trainingset->positions = malloc(ntot*sizeof(struct trainingpos));
    trainingset->size = 0;

    /* Open the training set */
    fp = fopen(file, "r");
    if (fp == NULL) {
        free_trainingset(trainingset);
        return NULL;
    }

    /* Read all training positions */
    str = fgets(buffer, sizeof(buffer), fp);
    while (str != NULL) {
        /* Make sure there is space left to store the position */
        if (trainingset->size == ntot) {
            ntot += 1000;
            trainingset->positions = realloc(trainingset->positions,
                                             ntot*sizeof(struct trainingpos));
            if (trainingset->positions == NULL) {
                return NULL;
            }
        }

        /* Extract result. Positions without a proper result are skipped. */
        if (strstr(buffer, "\"1-0\"")) {
            trainingset->positions[trainingset->size].result = 1;
        } else if (strstr(buffer, "\"0-1\"")) {
            trainingset->positions[trainingset->size].result = 0;
        } else if (strstr(buffer, "\"1/2-1/2\"")) {
            trainingset->positions[trainingset->size].result = 0.5;
        } else {
            str = fgets(buffer, sizeof(buffer), fp);
            continue;
        }

        /* Verify that the position is legal */
        board_reset(pos);
        if (!fen_setup_board(pos, buffer, true)) {
            str = fgets(buffer, sizeof(buffer), fp);
            continue;
        }

        /* Update training set */
        trainingset->positions[trainingset->size].epd = strdup(buffer);
        trainingset->size++;

        /* Next position */
        str = fgets(buffer, sizeof(buffer), fp);
    }

    /* Clean up */
    fclose(fp);

    return trainingset;
}

static void free_tuningset(struct tuningset *tuningset)
{
    if ((tuningset == NULL) || (tuningset->params == NULL)) {
        return;
    }

    tuning_param_destroy_list(tuningset->params);
    free(tuningset);
}

static struct tuningset* read_tuningset(char *file)
{
    FILE              *fp;
    char              buffer[512];
    struct tuningset  *tuningset;
    char              *line;
    char              name[256];
    int               value;
    int               idx;
    int               nconv;
    struct param_decl *decl;
    char              *iter;

    /* Allocate a tuningset */
    tuningset = malloc(sizeof(tuningset));
    tuningset->params = tuning_param_create_list();
    tuningset->size = NUM_TUNING_PARAMS;
    tuningset->nactive = 0;

    /* Open the parameter file */
    fp = fopen(file, "r");
    if (fp == NULL) {
        free_tuningset(tuningset);
        return NULL;
    }

    /* Read all parameters */
    line = fgets(buffer, sizeof(buffer), fp);
    while (line != NULL) {
        /* Skip comments */
        if (line[0] == '#') {
            line = fgets(buffer, sizeof(buffer), fp);
            continue;
        }

        /* Get the name of the parameter */
        nconv = sscanf(buffer, "%s ", name);
        if (nconv != 1) {
            line = fgets(buffer, sizeof(buffer), fp);
            continue;
        }

        /* Mark the parameter as active */
        decl = tuning_param_lookup(name);
        for (idx=decl->start;idx<=decl->stop;idx++) {
            tuningset->params[idx].active = true;
            tuningset->nactive++;
        }

        /* Set the initial value of the parameter */
        if (decl->start == decl->stop) {
            nconv = sscanf(buffer, "%s %d", name, &value);
            if (nconv != 2) {
                tuningset->params[decl->start].active = false;
            }
            tuningset->params[decl->start].current = value;
        } else {
            iter = strchr(buffer, '{');
            if (iter == NULL) {
                tuningset->params[decl->start].active = false;
                line = fgets(buffer, sizeof(buffer), fp);
                continue;
            }
            iter++;
            for (idx=decl->start;idx<decl->stop;idx++) {
                nconv = sscanf(iter, "%d,", &value);
                if (nconv != 1) {
                    tuningset->params[idx].active = false;
                    break;
                }
                tuningset->params[idx].current = value;
                iter = strchr(iter, ' ') + 1;
            }
            nconv = sscanf(iter, "%d}", &value);
            if (nconv != 1) {
                tuningset->params[decl->stop].active = false;
                line = fgets(buffer, sizeof(buffer), fp);
                continue;
            }
            tuningset->params[decl->stop].current = value;
        }

        /* Read the next parameter */
        line = fgets(buffer, sizeof(buffer), fp);
    }

    /* Assign values to tuning parameters */
    tuning_param_assign_current(tuningset->params);

    /* Clean up */
    fclose(fp);

    return tuningset;
}

void find_k(char *file, int nthreads)
{
    struct gamestate    *pos;
    struct trainingset  *trainingset;
    double              k;
    double              best_k;
    double              e;
    double              lowest_e;
    int                 iter;
    int                 pos_per_thread;
    int                 nextpos;
    int                 niterations;

    assert(file != NULL);

    printf("Finding K based on %s\n", file);

    /* Create game state */
    pos = create_game_state(DEFAULT_MAIN_HASH_SIZE);

    /* Read training set */
    trainingset = read_trainingset(pos, file);
    if (trainingset == NULL) {
        printf("Error: failed to read training set\n");
        return;
    }

    printf("Found %d training positions\n", trainingset->size);

    /* Initialize syncronization objects */
    pthread_cond_init(&workers_start_cond, NULL);
    pthread_cond_init(&workers_finished_cond, NULL);
    pthread_mutex_init(&worker_start_lock, NULL);
    pthread_mutex_init(&worker_finished_lock, NULL);

    /* Setup worker threads */
    pos_per_thread = trainingset->size/nthreads;
    nextpos = 0;
    nworkerthreads = nthreads;
    workers = malloc(sizeof(struct worker)*nworkerthreads);
    for (iter=0;iter<nworkerthreads;iter++) {
        workers[iter].state = WORKER_IDLE;
        workers[iter].trainingset = trainingset;
        workers[iter].first_pos = nextpos;
        workers[iter].last_pos = workers[iter].first_pos + pos_per_thread - 1;
        pthread_create(&workers[iter].thread, NULL, calc_texel_error_func,
                       &workers[iter]);
        nextpos = workers[iter].last_pos + 1;
    }

    /* Make sure all training positions are covered */
    workers[nworkerthreads-1].last_pos = trainingset->size - 1;

    /* Find the K that gives the lowest error */
    best_k = 0.0;
    lowest_e = 10.0;
    niterations = 0;
    for (k=K_MIN;k<K_MAX;k+=K_STEP) {
        /* Calculate error */
        e = calc_texel_error(trainingset, k);

        /* Check if the error has decreased */
        if (e < lowest_e) {
            best_k = k;
            lowest_e = e;
        }

        /* Display progress information */
        printf("#");
        niterations++;
        if ((niterations%50) == 0) {
            printf("\n");
        }
    }

    /* Stop all worker threads */
    pthread_mutex_lock(&worker_start_lock);
    for (iter=0;iter<nthreads;iter++) {
        workers[iter].k = k;
        workers[iter].state = WORKER_STOPPED;
    }
    pthread_cond_broadcast(&workers_start_cond);
    pthread_mutex_unlock(&worker_start_lock);

    /* Wait for all threads to exit */
    for (iter=0;iter<nthreads;iter++) {
        pthread_join(workers[iter].thread, NULL);
    }

    /* Print result */
    printf("\nK=%.3f, e=%.5f (%.2f%%)\n",
           best_k, lowest_e, sqrt(lowest_e)*100.0);

    /* Clean up */
    pthread_cond_destroy(&workers_start_cond);
    pthread_cond_destroy(&workers_finished_cond);
    pthread_mutex_destroy(&worker_start_lock);
    pthread_mutex_destroy(&worker_finished_lock);
    free(workers);
    free_trainingset(trainingset);
    destroy_game_state(pos);
}

void tune_parameters(char *training_file, char *parameter_file, int nthreads,
                     int stepsize)
{
    struct tuningset    *tuningset;
    struct trainingset  *trainingset;
    struct gamestate    *pos;
    int                 pos_per_thread;
    int                 nextpos;
    int                 k;
    FILE                *fp;

    assert(training_file != NULL);
    assert(parameter_file != NULL);

    printf("Tuning parameters in %s based on the training set %s\n",
           parameter_file, training_file);

    /* Create game state */
    pos = create_game_state(DEFAULT_MAIN_HASH_SIZE);

    /* Read tuning set */
    tuningset = read_tuningset(parameter_file);
    if (tuningset == NULL) {
        printf("Error: failed to read tuning set\n");
        return;
    }

    printf("Found %d parameter(s) to tune\n", tuningset->nactive);

    /* Read training set */
    trainingset = read_trainingset(pos, training_file);
    if (trainingset == NULL) {
        printf("Error: failed to read training set\n");
        return;
    }

    printf("Found %d training positions\n", trainingset->size);

    /* Initialize syncronization objects */
    pthread_cond_init(&workers_start_cond, NULL);
    pthread_cond_init(&workers_finished_cond, NULL);
    pthread_mutex_init(&worker_start_lock, NULL);
    pthread_mutex_init(&worker_finished_lock, NULL);

    /* Setup worker threads */
    pos_per_thread = trainingset->size/nthreads;
    nextpos = 0;
    nworkerthreads = nthreads;
    workers = malloc(sizeof(struct worker)*nthreads);
    for (k=0;k<nthreads;k++) {
        workers[k].state = WORKER_IDLE;
        workers[k].trainingset = trainingset;
        workers[k].first_pos = nextpos;
        workers[k].last_pos = workers[k].first_pos + pos_per_thread - 1;
        pthread_create(&workers[k].thread, NULL, calc_texel_error_func,
                       &workers[k]);
        nextpos = workers[k].last_pos + 1;
    }

    /* Make sure all training positions are covered */
    workers[nthreads-1].last_pos = trainingset->size - 1;

    printf("Tuning parameters\n");

    /* Optimize the tuning set */
    local_optimize(tuningset, trainingset, stepsize);

    /* Write tuning result */
    printf("\n");
    printf("Parameter values:\n");
    tuning_param_write_parameters(stdout, tuningset->params, true);
    fp = fopen(TUNING_FINAL_RESULT_FILE, "w");
    if (fp != NULL) {
        tuning_param_write_parameters(fp, tuningset->params, true);
        fclose(fp);
    }

    /* Clean up */
    free_tuningset(tuningset);
    free_trainingset(trainingset);
    destroy_game_state(pos);
}

static void print_parameters(char *output_file)
{
    FILE                *fp;
    struct tuning_param *params;

    fp = fopen(output_file, "w");
    if (fp == NULL) {
        printf("Failed to open output file\n");
        return;
    }

    params = tuning_param_create_list();
    tuning_param_write_parameters(fp, params, false);

    tuning_param_destroy_list(params);
    fclose(fp);
}

static void print_usage(void)
{
    printf("Usage: %s [options]\n", APP_NAME);
    printf("Options:\n");
    printf("\t-k <training file>\n\tCalculate the tuning constant K\n\n");
    printf("\t-t <training file> <parameter file>\n\tTune parameters\n\n");
    printf("\t-p <output file>\n\tPrint all tunable parameters\n\n");
    printf("\t-n <nthreads>\n\tThe number of threads to use\n\n");
    printf("\t-s <stepsize>\n\tStep size for first iteration\n\n");
    printf("\t-h\n\tDisplay this message\n\n");
}

int main(int argc, char *argv[])
{
    int  iter;
    int  nconv;
    char *training_file;
    char *parameter_file;
    char *output_file;
    int  nthreads;
    int  command;
    int  stepsize;

    /* Turn off buffering for I/O */
    setbuf(stdout, NULL);
    setbuf(stdin, NULL);

    /* Seed random number generator */
    srand((unsigned int)time(NULL));

    /* Initialize components */
    dbg_log_init(0);
    chess_data_init();
    bb_init();
    eval_reset();

    /* Initialize options */
    training_file = NULL;
    parameter_file = NULL;
    output_file = NULL;
    nthreads = 1;
    stepsize = 1;

    /* Parse command line arguments */
    iter = 1;
    command = -1;
    while (iter < argc) {
        if (!strcmp(argv[iter], "-h")) {
            print_usage();
            exit(0);
        } else if (!strcmp(argv[iter], "-k")) {
            command = 0;
            iter++;
            if (iter == argc) {
                printf("Invalid argument\n");
                print_usage();
                exit(1);
            }
            training_file = argv[iter];
        } else if (!strcmp(argv[iter], "-t")) {
            command = 1;
            iter++;
            if (iter == argc) {
                printf("Invalid argument\n");
                print_usage();
                exit(1);
            }
            training_file = argv[iter];
            iter++;
            if (iter == argc) {
                printf("Invalid argument\n");
                print_usage();
                exit(1);
            }
            parameter_file = argv[iter];
        } else if (!strcmp(argv[iter], "-n")) {
            iter++;
            if (iter == argc) {
                printf("Invalid argument\n");
                print_usage();
                exit(1);
            }
            nconv = sscanf(argv[iter], "%u", &nthreads);
            if (nconv != 1) {
                printf("Invalid argument\n");
                print_usage();
                exit(1);
            }
        } else if (!strcmp(argv[iter], "-p")) {
            command = 2;
            iter++;
            if (iter == argc) {
                printf("Invalid argument\n");
                print_usage();
                exit(1);
            }
            output_file = argv[iter];
        } else if (!strcmp(argv[iter], "-s")) {
            iter++;
            if (iter == argc) {
                printf("Invalid argument\n");
                print_usage();
                exit(1);
            }
            nconv = sscanf(argv[iter], "%u", &stepsize);
            if (nconv != 1) {
                printf("Invalid argument\n");
                print_usage();
                exit(1);
            }
        } else {
            printf("Invalid argument\n");
            print_usage();
            exit(1);
        }
        iter++;
    }

    /* Execute command */
    switch (command) {
    case 0:
        find_k(training_file, nthreads);
        break;
    case 1:
        tune_parameters(training_file, parameter_file, nthreads, stepsize);
        break;
    case 2:
        print_parameters(output_file);
        break;
    default:
        print_usage();
        exit(1);
        break;
    }

    return 0;
}
