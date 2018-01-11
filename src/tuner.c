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
#include "thread.h"
#include "trace.h"

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

/* Equation term */
struct term {
    int param;
    int multiplier;
    int divisor;
};

/*
 * Equation describing the evaluation function
 * for a specific position.
 */
struct eval_equation {
    int         phase;
    int         base[NPHASES][NSIDES];
    int         nterms[NPHASES][NSIDES];
    struct term *terms[NPHASES][NSIDES];
};

/* Training position */
struct trainingpos {
    char                 *epd;
    char                 *fen_quiet;
    double               result;
    bool                 has_equation;
    struct eval_equation equation;
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
struct tuning_worker {
    thread_t            thread;
    event_t             ev_start;
    event_t             ev_done;
    struct trainingset  *trainingset;
    struct tuningset    *tuningset;
    int                 first_pos;
    int                 last_pos;
    double              k;
    double              sum;
    bool                update_pv;
    enum worker_state   state;
};

/* Workers used for calculating errors */
struct tuning_worker *workers = NULL;
static int    nworkerthreads = 0;

/* Function declarations */
static void* calc_texel_error_func(void *data);

static void mark_pv_for_update(void)
{
    int k;

    for (k=0;k<nworkerthreads;k++) {
        workers[k].update_pv = true;
    }
}

static void init_workers(struct trainingset *trainingset,
                         struct tuningset *tuningset)
{
    int iter;
    int pos_per_thread;
    int nextpos;

    pos_per_thread = trainingset->size/nworkerthreads;
    nextpos = 0;
    for (iter=0;iter<nworkerthreads;iter++) {
        workers[iter].state = WORKER_IDLE;
        workers[iter].trainingset = trainingset;
        workers[iter].tuningset = tuningset;
        workers[iter].first_pos = nextpos;
        workers[iter].last_pos = workers[iter].first_pos + pos_per_thread - 1;
        nextpos = workers[iter].last_pos + 1;

        thread_create(&workers[iter].thread, calc_texel_error_func,
                      &workers[iter]);
        event_init(&workers[iter].ev_start);
        event_init(&workers[iter].ev_done);
    }
}

static void destroy_workers(void)
{
    int iter;

    for (iter=0;iter<nworkerthreads;iter++) {
        event_destroy(&workers[iter].ev_start);
        event_destroy(&workers[iter].ev_done);
    }
}

static struct term* setup_equation_terms(struct eval_trace *trace, int phase,
                                         int side, int *nterms)
{
    struct term *terms;
    int         count;
    int         k;

    /* Count the number of terms */
    count = 0;
    for (k=0;k<NUM_TUNING_PARAMS;k++) {
        if (trace->params[k].multiplier[phase][side] > 0) {
            count++;
        }
    }

    /* Allocate terms */
    terms = malloc(sizeof(struct term)*count);

    /* Setup terms */
    count = 0;
    for (k=0;k<NUM_TUNING_PARAMS;k++) {
        if (trace->params[k].multiplier[phase][side] > 0) {
            terms[count].param = k;
            terms[count].multiplier = trace->params[k].multiplier[phase][side];
            terms[count].divisor = trace->params[k].divisor[phase][side];
            count++;
        }
    }
    *nterms = count;

    return terms;
}

static void setup_eval_equation(struct eval_trace *trace,
                                struct eval_equation *equation)
{
    int phase;
    int side;

    equation->phase = trace->phase;
    for (phase=0;phase<=1;phase++) {
        for (side=0;side<=1;side++) {
            equation->base[phase][side] = trace->base[phase][side];
            equation->terms[phase][side] = setup_equation_terms(trace, phase,
                                        side, &equation->nterms[phase][side]);
        }
    }
}

static int summarize_term(struct term *term, struct tuningset *tuningset)
{
    int val;

    val = tuningset->params[term->param].current;
    val *= term->multiplier;
    if (term->divisor > 0) {
        val /= term->divisor;
    }

    return val;
}

static int summarize_pawn_shield_terms(struct eval_equation *equation,
                                       struct tuningset *tuningset, int phase,
                                       int side, int *index)
{
    struct term *term;
    int         val;

    term = &equation->terms[phase][side][*index];
    val = summarize_term(term, tuningset);

    term = &equation->terms[phase][side][*index+1];
    if ((term->param == TP_PAWN_SHIELD_RANK2) ||
        (term->param == TP_PAWN_SHIELD_HOLE)) {
        (*index)++;
        val += summarize_term(term, tuningset);

        term = &equation->terms[phase][side][*index+1];
        if (term->param == TP_PAWN_SHIELD_HOLE) {
            (*index)++;
            val += summarize_term(term, tuningset);
        }
    }

    return MAX(val, 0);
}

static int evaluate_equation(struct position *pos, struct tuningset *tuningset,
                             struct trainingpos  *trainingpos)
{
    struct eval_equation *equation;
    struct term          *term;
    int                  score[NPHASES][NSIDES];
    int                  score_mg;
    int                  score_eg;
    int                  phase;
    int                  side;
    int                  k;

    equation = &trainingpos->equation;
    for (phase=0;phase<=1;phase++) {
        for (side=0;side<=1;side++) {
            score[phase][side] = equation->base[phase][side];
            for (k=0;k<equation->nterms[phase][side];k++) {
                term = &equation->terms[phase][side][k];
                if ((term->param == TP_PAWN_SHIELD_RANK1) ||
                    (term->param == TP_PAWN_SHIELD_RANK2) ||
                    (term->param == TP_PAWN_SHIELD_HOLE)) {
                    score[phase][side] += summarize_pawn_shield_terms(equation,
                                                                      tuningset,
                                                                      phase,
                                                                      side, &k);
                } else {
                    score[phase][side] += summarize_term(term, tuningset);
                }
            }
        }
    }

    score_mg = score[MIDDLEGAME][WHITE] - score[MIDDLEGAME][BLACK];
    score_eg = score[ENDGAME][WHITE] - score[ENDGAME][BLACK];
    score_mg = (pos->stm == WHITE)?score_mg:-score_mg;
    score_eg = (pos->stm == WHITE)?score_eg:-score_eg;

    return ((score_mg*(256-equation->phase)) + (score_eg*equation->phase))/256;
}

void print_equation(struct eval_equation *equation, int endgame, int side)
{
    struct term *term;
    char *phase;
    char *player;
    int  k;

    phase = endgame?"endgame":"middlegame";
    player = (side == WHITE)?"white":"black";

    printf("%s, %s\n", phase, player);
    printf("phase: %d\n", equation->phase);
    printf("base: %d\n", equation->base[endgame][side]);
    for (k=0;k<equation->nterms[endgame][side];k++) {
        term = &equation->terms[endgame][side][k];
        printf("param %d = %d", term->param, term->multiplier);
        if (term->divisor != 0) {
            printf("/%d\n", term->divisor);
        } else {
            printf("\n");
        }
    }
}

static void find_quiet_trainingset(struct gamestate *state,
                                   struct tuning_worker *worker)
{
    int                iter;
    struct pv          *pv;
    struct trainingset *trainingset;
    int                k;
    char               fenstr[FEN_MAX_LENGTH];

    /* Iterate over all training positions */
    pv = malloc(sizeof(struct pv));
    memset(pv, 0, sizeof(struct pv));
    trainingset = worker->trainingset;
    for (iter=worker->first_pos;iter<=worker->last_pos;iter++) {
        /* Setup position */
        board_reset(&state->pos);
        (void)fen_setup_board(&state->pos, trainingset->positions[iter].epd,
                              true);

        /* Do a quiscence search to get the pv */
        (void)search_get_quiscence_score(state, pv);

        /* Find the position at the end of the pv */
        for (k=0;k<pv->length;k++) {
           board_make_move(&state->pos, pv->moves[k]);
        }
        fen_build_string(&state->pos, fenstr);
        free(trainingset->positions[iter].fen_quiet);
        trainingset->positions[iter].fen_quiet = strdup(fenstr);
    }
    free(pv);
}

static double calc_texel_sigmoid(int score, double k)
{
    double exp;

    exp = -(k*((double)score)/400.0);
    return 1.0/(1.0 + pow(10.0, exp));
}

static void* calc_texel_error_func(void *data)
{
    struct tuning_worker *worker;
    struct gamestate     *state;
    struct trainingset   *trainingset;
    int                  iter;
    int                  score;
    double               sigmoid;
    double               result;
    struct eval_trace    *trace;

    /* Initialize worker */
    worker = (struct tuning_worker*)data;
    state = create_game_state();
    trainingset = worker->trainingset;

    /* Worker main loop */
    while (true) {
        /* Wait for signal to start */
        event_wait(&worker->ev_start);
        if (worker->state == WORKER_STOPPED) {
            break;
        }

        /* Check if the pv should be updated */
        if (worker->update_pv) {
            find_quiet_trainingset(state, worker);
            worker->update_pv = false;
        }

        /* Iterate over all training positions assigned to this worker */
        worker->sum = 0.0;
        for (iter=worker->first_pos;iter<=worker->last_pos;iter++) {
            /*
             * Trace the evaluation function for this position
             * and create a corresponding equation
             */
            if (!trainingset->positions[iter].has_equation) {
                board_reset(&state->pos);
                (void)fen_setup_board(&state->pos,
                                      trainingset->positions[iter].fen_quiet,
                                      true);
                trace = malloc(sizeof(struct eval_trace));
                eval_generate_trace(&state->pos, trace);
                setup_eval_equation(trace,
                                    &trainingset->positions[iter].equation);
                trainingset->positions[iter].has_equation = true;
                free(trace);
                trace = NULL;
            }

            /* Evaluate the equation */
            score = evaluate_equation(&state->pos, worker->tuningset,
                                      &trainingset->positions[iter]);
            score = (state->pos.stm == WHITE)?score:-score;

            /* Calculate error */
            sigmoid = calc_texel_sigmoid(score, worker->k);
            result = trainingset->positions[iter].result;
            worker->sum += ((result - sigmoid)*(result - sigmoid));
        }

        /* Signal that this iteration is finished */
        worker->state = WORKER_FINISHED;
        event_set(&worker->ev_done);
    }

    /* Clean up */
    destroy_game_state(state);

    return NULL;
}

static double calc_texel_error(struct trainingset *trainingset, double k)
{
    int     iter;
    double  sum;

    /* Start all worker threads */
    for (iter=0;iter<nworkerthreads;iter++) {
        workers[iter].k = k;
        workers[iter].state = WORKER_RUNNING;
        event_set(&workers[iter].ev_start);
    }

    /* Wait for all workers to finish */
    for (iter=0;iter<nworkerthreads;iter++) {
        event_wait(&workers[iter].ev_done);
    }

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
    int     count;
    int     delta;

    /* Generate a quiet trainingset and calculate the initial error */
    mark_pv_for_update();
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
        free(trainingset->positions[k].equation.terms[MIDDLEGAME][WHITE]);
        free(trainingset->positions[k].equation.terms[MIDDLEGAME][BLACK]);
        free(trainingset->positions[k].equation.terms[ENDGAME][WHITE]);
        free(trainingset->positions[k].equation.terms[ENDGAME][BLACK]);
    }
    free(trainingset->positions);
    free(trainingset);
}

static struct trainingset* read_trainingset(struct gamestate *state, char *file)
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
    trainingset = malloc(sizeof(struct trainingset));
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
        board_reset(&state->pos);
        if (!fen_setup_board(&state->pos, buffer, true)) {
            str = fgets(buffer, sizeof(buffer), fp);
            continue;
        }

        /* Update training set */
        trainingset->positions[trainingset->size].epd = strdup(buffer);
        trainingset->positions[trainingset->size].fen_quiet = NULL;
        trainingset->positions[trainingset->size].has_equation = false;
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
    tuningset = malloc(sizeof(struct tuningset));
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
    struct gamestate    *state;
    struct trainingset  *trainingset;
    struct tuningset    *tuningset;
    double              k;
    double              best_k;
    double              e;
    double              lowest_e;
    int                 iter;
    int                 niterations;

    assert(file != NULL);

    printf("Finding K based on %s\n", file);

    /* Create game state */
    state = create_game_state();

    /* Read training set */
    trainingset = read_trainingset(state, file);
    if (trainingset == NULL) {
        printf("Error: failed to read training set\n");
        return;
    }

    /* Allocate a tuningset */
    tuningset = malloc(sizeof(struct tuningset));
    tuningset->params = tuning_param_create_list();
    tuningset->size = NUM_TUNING_PARAMS;
    tuningset->nactive = 0;

    printf("Found %d training positions\n", trainingset->size);

    /* Setup worker threads */
    nworkerthreads = nthreads;
    workers = malloc(sizeof(struct tuning_worker)*nworkerthreads);
    init_workers(trainingset, tuningset);
    mark_pv_for_update();

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
    for (iter=0;iter<nthreads;iter++) {
        workers[iter].k = k;
        workers[iter].state = WORKER_STOPPED;
        event_set(&workers[iter].ev_start);
    }

    /* Wait for all threads to exit */
    for (iter=0;iter<nthreads;iter++) {
        thread_join(&workers[iter].thread);
    }

    /* Print result */
    printf("\nK=%.3f, e=%.5f (%.2f%%)\n",
           best_k, lowest_e, sqrt(lowest_e)*100.0);

    /* Clean up */
    destroy_workers();
    free(workers);
    free_trainingset(trainingset);
    free_tuningset(tuningset);
    destroy_game_state(state);
}

void tune_parameters(char *training_file, char *parameter_file, int nthreads,
                     int stepsize)
{
    struct tuningset    *tuningset;
    struct trainingset  *trainingset;
    struct gamestate    *state;
    FILE                *fp;
    time_t              start;
    time_t              diff;
    int                 hh;
    int                 mm;
    int                 ss;

    assert(training_file != NULL);
    assert(parameter_file != NULL);

    printf("Tuning parameters in %s based on the training set %s\n",
           parameter_file, training_file);

    /* Remeber when we start tuning */
    start = get_current_time();

    /* Create game state */
    state = create_game_state();

    /* Read tuning set */
    tuningset = read_tuningset(parameter_file);
    if (tuningset == NULL) {
        printf("Error: failed to read tuning set\n");
        return;
    }

    printf("Found %d parameter(s) to tune\n", tuningset->nactive);

    /* Read training set */
    trainingset = read_trainingset(state, training_file);
    if (trainingset == NULL) {
        printf("Error: failed to read training set\n");
        return;
    }

    printf("Found %d training positions\n", trainingset->size);

    /* Setup worker threads */
    nworkerthreads = nthreads;
    workers = malloc(sizeof(struct tuning_worker)*nthreads);
    init_workers(trainingset, tuningset);

    /* Make sure all training positions are covered */
    workers[nthreads-1].last_pos = trainingset->size - 1;

    printf("\n");

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

    /* Print timing result */
    diff = (get_current_time() - start)/1000;
    hh = diff/3600;
    diff = diff%3600;
    mm = diff/60;
    diff = diff%60;
    ss = diff;
    printf("\nTime: %02d:%02d:%02d\n", hh, mm, ss);

    /* Clean up */
    destroy_workers();
    free(workers);
    free_tuningset(tuningset);
    free_trainingset(trainingset);
    destroy_game_state(state);
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

static void verify_trace(char *training_file)
{
    struct trainingset *trainingset;
    struct tuningset   *tuningset;
    struct gamestate   *state;
    int                k;
    int                score;
    int                score2;
    struct eval_trace  *trace;

    assert(training_file != NULL);

    /* Create game state */
    state = create_game_state();

    /* Allocate a tuningset */
    tuningset = malloc(sizeof(struct tuningset));
    tuningset->params = tuning_param_create_list();
    tuningset->size = NUM_TUNING_PARAMS;
    tuningset->nactive = 0;
    tuning_param_assign_current(tuningset->params);

    /* Read training set */
    trainingset = read_trainingset(state, training_file);
    if (trainingset == NULL) {
        printf("Error: failed to read training set\n");
        return;
    }

    /* Iterate over all positions */
    for (k=0;k<trainingset->size;k++) {
        /* Setup position */
        board_reset(&state->pos);
        (void)fen_setup_board(&state->pos, trainingset->positions[k].epd,
                              true);

        /* Evaluate the position */
        score = eval_evaluate_full(&state->pos, false);

        /* Generate a trace for this function */
        trace = malloc(sizeof(struct eval_trace));
        eval_generate_trace(&state->pos, trace);

        /* Setup an equation and evaluate it */
        setup_eval_equation(trace, &trainingset->positions[k].equation);
        score2 = evaluate_equation(&state->pos, tuningset,
                                   &trainingset->positions[k]);
        free(trace);
        trace = NULL;

        /* Check that the scores match */
        if (score != score2) {
            printf("Wrong score (%d): %d (%d)\n", k, score2, score);
            printf("%s", trainingset->positions[k].epd);
            print_equation(&trainingset->positions[k].equation, false, WHITE);
            print_equation(&trainingset->positions[k].equation, true, WHITE);
            print_equation(&trainingset->positions[k].equation, false, BLACK);
            print_equation(&trainingset->positions[k].equation, true, BLACK);
            printf("\n");
        }
    }

    /* Clean up */
    free_trainingset(trainingset);
    free_tuningset(tuningset);
    destroy_game_state(state);
}

static void print_usage(void)
{
    printf("Usage: tuner [options]\n");
    printf("Options:\n");
    printf("\t-k <training file>\n\tCalculate the tuning constant K\n\n");
    printf("\t-v <training file>\n\tVerify evaluation tracing\n\n");
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
        } else if (!strcmp(argv[iter], "-v")) {
            command = 3;
            iter++;
            if (iter == argc) {
                printf("Invalid argument\n");
                print_usage();
                exit(1);
            }
            training_file = argv[iter];

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
    case 3:
        verify_trace(training_file);
        break;
    default:
        print_usage();
        exit(1);
        break;
    }

    return 0;
}
