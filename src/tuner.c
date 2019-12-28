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
#ifndef WINDOWS
#include <signal.h>
#endif

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
#include "see.h"

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

/* Constants for Adam */
#define BETA1 0.9
#define BETA2 0.999
#define EPSILON 1.0E-8
#define STEP_SIZE 0.1
#define REPORT_INTERVAL 100
#define DEFAULT_ITERATIONS 100000

/* Constants for L2 regularization */
#define LAMBDA 1E-7

enum optimization_algorithm {
    OPT_LOCAL_SEARCH,
    OPT_ADAM
};

/* Equation term */
struct term {
    int    param_id;
    double fact;
    double scale;
};

/*
 * Equation describing the evaluation function
 * for a specific position.
 */
struct eval_equation {
    double      base;
    int         nterms;
    struct term *terms;
};

/* Training position */
struct trainingpos {
    char                 *epd;
    double               result;
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

/* Worker thread */
struct tuning_worker {
    thread_t           thread;
    struct trainingset *trainingset;
    struct tuningset   *tuningset;
    int                first_pos;
    int                last_pos;
    double             val;
    double             gradients[NUM_TUNING_PARAMS];
};

/* Workers used for calculating errors */
struct tuning_worker *workers = NULL;
static int nworkerthreads = 0;
static double scaling_constant = K;
static volatile bool stop_optimization = false;
static bool regularize = true;

static void init_workers(struct trainingset *trainingset,
                         struct tuningset *tuningset)
{
    int iter;
    int pos_per_thread;
    int nextpos;

    pos_per_thread = trainingset->size/nworkerthreads;
    nextpos = 0;
    for (iter=0;iter<nworkerthreads;iter++) {
        workers[iter].trainingset = trainingset;
        workers[iter].tuningset = tuningset;
        workers[iter].first_pos = nextpos;
        workers[iter].last_pos = workers[iter].first_pos + pos_per_thread - 1;
        nextpos = workers[iter].last_pos + 1;
    }
}

static void setup_term(struct term *term, struct trace_param *param,
                       int param_id, int phase_factor)
{
    double temp;
    double fact[NPHASES];
    int    phase;

    term->param_id = param_id;

    for (phase=0;phase<NPHASES;phase++) {
        temp = param->mul[phase][WHITE];
        if (param->div[phase][WHITE] > 0) {
            temp /= param->div[phase][WHITE];
        }
        fact[phase] = temp;

        temp = param->mul[phase][BLACK];
        if (param->div[phase][BLACK] > 0) {
            temp /= param->div[phase][BLACK];
        }
        fact[phase] -= temp;
    }

    if ((fact[MIDDLEGAME] != 0) && (fact[ENDGAME] != 0)) {
        assert(fact[MIDDLEGAME] == fact[ENDGAME]);
        term->fact = fact[MIDDLEGAME];
        term->scale = 1;
    } else if (fact[MIDDLEGAME] != 0) {
        term->fact = fact[MIDDLEGAME];
        term->scale = (256.0-phase_factor)/256.0;
    } else {
        term->fact = fact[ENDGAME];
        term->scale = phase_factor/256.0;
    }
}

static void setup_eval_equation(struct eval_trace *trace,
                                struct eval_equation *equation)
{
    int k;
    int idx;
    int count;

    /* Count the number of non-zero parameters */
    count = 0;
    for (k=0;k<NUM_TUNING_PARAMS;k++) {
        if ((trace->params[k].mul[MIDDLEGAME][WHITE] != 0) ||
            (trace->params[k].mul[MIDDLEGAME][BLACK] != 0) ||
            (trace->params[k].mul[ENDGAME][WHITE] != 0) ||
            (trace->params[k].mul[ENDGAME][BLACK] != 0)) {
            count++;
        }
    }

    /* Allocate terms */
    equation->terms = malloc(sizeof(struct term)*count);
    equation->nterms = count;

    /* Setup base score */
    assert(trace->base[MIDDLEGAME][WHITE] == trace->base[ENDGAME][WHITE]);
    assert(trace->base[MIDDLEGAME][BLACK] == trace->base[ENDGAME][BLACK]);
    equation->base = trace->base[ENDGAME][WHITE] - trace->base[ENDGAME][BLACK];

    /* Setup terms */
    idx = 0;
    for (k=0;k<NUM_TUNING_PARAMS;k++) {
        if ((trace->params[k].mul[MIDDLEGAME][WHITE] == 0) &&
            (trace->params[k].mul[MIDDLEGAME][BLACK] == 0) &&
            (trace->params[k].mul[ENDGAME][WHITE] == 0) &&
            (trace->params[k].mul[ENDGAME][BLACK] == 0)) {
            continue;
        }

        setup_term(&equation->terms[idx], &trace->params[k], k,
                   trace->phase_factor);
        idx++;
    }
}

static double evaluate_term(struct term *term, struct tuningset *tuningset)
{
    return tuningset->params[term->param_id].current*term->fact*term->scale;
}

static double evaluate_equation(struct eval_equation *equation,
                                struct tuningset *tuningset)
{
    double score;
    int    k;

    score = equation->base;
    for (k=0;k<equation->nterms;k++) {
        score += evaluate_term(&equation->terms[k], tuningset);
    }

    return score;
}

void print_equation(struct eval_equation *equation)
{
    struct term *term;
    int         k;

    printf("base: %f\n", equation->base);
    for (k=0;k<equation->nterms;k++) {
        term = &equation->terms[k];
        printf("param %d: %f, %f\n", term->param_id, term->fact, term->scale);
    }
}

static thread_retval_t trace_positions_func(void *data)
{
    struct tuning_worker *worker;
    struct gamestate     *state;
    struct trainingset   *trainingset;
    int                  iter;
    struct eval_trace    *trace;
    struct pv            *pv;

    worker = (struct tuning_worker*)data;
    state = create_game_state();
    pv = malloc(sizeof(struct movelist));
    memset(pv, 0, sizeof(struct movelist));
    trainingset = worker->trainingset;

    /* Iterate over all training positions assigned to this worker */
    for (iter=worker->first_pos;iter<=worker->last_pos;iter++) {
        /* Setup position */
        board_reset(&state->pos);
        (void)fen_setup_board(&state->pos, trainingset->positions[iter].epd,
                              true);

        /*
         * Trace the evaluation function for this position
         * and create a corresponding equation
         */
        trace = malloc(sizeof(struct eval_trace));
        eval_generate_trace(&state->pos, trace);
        setup_eval_equation(trace, &trainingset->positions[iter].equation);
        free(trace);
    }

    /* Clean up */
    free(pv);
    destroy_game_state(state);

    return (thread_retval_t)0;
}

static void trace_positions(void)
{
    int iter;

    /* Start all worker threads */
    for (iter=0;iter<nworkerthreads;iter++) {
        thread_create(&workers[iter].thread, trace_positions_func,
                      &workers[iter]);
    }

    /* Wait for all workers to finish */
    for (iter=0;iter<nworkerthreads;iter++) {
        thread_join(&workers[iter].thread);
    }
}

static double texel_sigmoid(double score)
{
    double exp;

    exp = -(scaling_constant*score/400.0);
    return 1.0/(1.0 + pow(10.0, exp));
}

static double texel_error(double score, double result)
{
    return result - texel_sigmoid(score);
}

static double texel_squared_error(double score, double result)
{
    double error;

    error = texel_error(score, result);
    return error*error;
}

static thread_retval_t calc_texel_squared_error_func(void *data)
{
    struct tuning_worker *worker;
    struct gamestate     *state;
    struct trainingset   *trainingset;
    int                  iter;
    double               score;

    /* Initialize worker */
    worker = (struct tuning_worker*)data;
    state = create_game_state();
    trainingset = worker->trainingset;

    /* Iterate over all training positions assigned to this worker */
    worker->val = 0.0;
    for (iter=worker->first_pos;iter<=worker->last_pos;iter++) {
        /* Evaluate the equation */
        score = evaluate_equation(&trainingset->positions[iter].equation,
                                  worker->tuningset);

        /* Calculate error */
        worker->val += texel_squared_error(score,
                                           trainingset->positions[iter].result);
    }

    /* Clean up */
    destroy_game_state(state);

    return (thread_retval_t)0;
}

static double calc_texel_squared_error(struct trainingset *trainingset)
{
    int     iter;
    double  error;

    /* Start all worker threads */
    for (iter=0;iter<nworkerthreads;iter++) {
        thread_create(&workers[iter].thread, calc_texel_squared_error_func,
                      &workers[iter]);
    }

    /* Wait for all workers to finish */
    for (iter=0;iter<nworkerthreads;iter++) {
        thread_join(&workers[iter].thread);
    }

    /* Summarize the result of all workers and calculate the error */
    error = 0.0;
    for (iter=0;iter<nworkerthreads;iter++) {
        error += workers[iter].val;
    }

    return error/(double)trainingset->size;
}

#ifndef WINDOWS
static void signal_handler(int signum)
{
    if (signum == SIGINT) {
        stop_optimization = true;
    }
}
#endif

static thread_retval_t calc_texel_gradients_func(void *data)
{
    struct tuning_worker *worker;
    struct trainingset   *trainingset;
    struct eval_equation *equation;
    struct term          *term;
    double               score;
    double               error;
    int                  iter;
    int                  k;

    /* Initialize worker */
    worker = (struct tuning_worker*)data;
    trainingset = worker->trainingset;
    for (k=0;k<NUM_TUNING_PARAMS;k++) {
        worker->gradients[k] = 0.0;
    }

    for (iter=worker->first_pos;iter<=worker->last_pos;iter++) {
        equation = &trainingset->positions[iter].equation;
        score = evaluate_equation(equation, worker->tuningset);
        error = texel_error(score, trainingset->positions[iter].result);

        for (k=0;k<equation->nterms;k++) {
            term = &equation->terms[k];
            worker->gradients[term->param_id] += (error*term->fact*term->scale);
        }
    }

    return (thread_retval_t)0;
}

static void calc_texel_gradients(double *gradients)
{
    int iter;
    int k;

    /* Start all worker threads */
    for (iter=0;iter<nworkerthreads;iter++) {
        thread_create(&workers[iter].thread, calc_texel_gradients_func,
                      &workers[iter]);
    }

    /* Wait for all workers to finish */
    for (iter=0;iter<nworkerthreads;iter++) {
        thread_join(&workers[iter].thread);
    }

    /* Summarize the result of all workers and calculate the error */
    for (k=0;k<NUM_TUNING_PARAMS;k++) {
        gradients[k] = 0.0;
        for (iter=0;iter<nworkerthreads;iter++) {
            gradients[k] += (workers[iter].gradients[k]);
        }
        gradients[k] *= (-2.0/workers[0].trainingset->size);
        if (regularize) {
            gradients[k] += (2*LAMBDA*workers[0].tuningset->params[k].current);
        }
    }
}

static void adam(struct tuningset *tuningset, struct trainingset *trainingset,
                 int max_iterations, double step_size)
{
    struct tuning_param *param;
    double              m[NUM_TUNING_PARAMS] = {0.0};
    double              v[NUM_TUNING_PARAMS] = {0.0};
    double              gradients[NUM_TUNING_PARAMS];
    double              m_hat;
    double              v_hat;
    int                 k;
    int                 niterations;
    double              error;
    double              prev_error;

#ifndef WINDOWS
    struct sigaction sa = {0};
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
#endif

    trace_positions();
    tuning_param_assign_current(tuningset->params);
    error = calc_texel_squared_error(trainingset);
    printf("Initial error: %f\n", error);
    prev_error = error;

    printf("\nOptimizing using Adam\n");
    if (regularize) {
        printf("Applying L2 regularization\n");
    }
    printf("\n");

    for (niterations=1;niterations<=max_iterations;niterations++) {
        /* Check if the user has interrupted the optimization */
        if (stop_optimization) {
            break;
        }

        /* Calculate the gradient for each parameter */
        calc_texel_gradients(gradients);

        /* Update the parameters that are being tuned */
        for (k=0;k<NUM_TUNING_PARAMS;k++) {
            param = &tuningset->params[k];
            if (!param->active) {
                continue;
            }

            m[k] = BETA1*m[k] + (1 - BETA1)*gradients[k];
            v[k] = BETA2*v[k] + (1 - BETA2)*gradients[k]*gradients[k];
            m_hat = m[k]/(1 - pow(BETA1, niterations));
            v_hat = v[k]/(1 - pow(BETA2, niterations));
            param->current -= ((step_size/(sqrt(v_hat) + EPSILON))*m_hat);
            param->current = CLAMP(param->current, param->min, param->max);
        }
        tuning_param_assign_current(tuningset->params);

        /* Display regular progress */
        if ((niterations%REPORT_INTERVAL) == 0) {
            error = calc_texel_squared_error(trainingset);
            if (error >= prev_error) {
                break;
            }
            prev_error = error;

            printf("Iteration: %d, Error: %f\n", niterations, error);
        }
    }

    error = calc_texel_squared_error(trainingset);
    printf("\n");
    printf("Total number of iterations: %d\n", niterations);
    printf("Final error: %f\n", error);
}

static void local_search(struct tuningset *tuningset,
                         struct trainingset *trainingset)
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
    trace_positions();
    tuning_param_assign_current(tuningset->params);
    best_e = calc_texel_squared_error(trainingset);
    printf("Initial error: %f\n", best_e);

    /* Loop until no more improvements are found */
    printf("\nOptimizing using local search\n\n");
    delta = 1;
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
                e = calc_texel_squared_error(trainingset);
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
                    e = calc_texel_squared_error(trainingset);
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
        free(trainingset->positions[k].equation.terms);
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
    trace_positions();

    /* Make sure all training positions are covered */
    workers[nworkerthreads-1].last_pos = trainingset->size - 1;

    /* Find the K that gives the lowest error */
    best_k = 0.0;
    lowest_e = 10.0;
    niterations = 0;
    for (k=K_MIN;k<K_MAX;k+=K_STEP) {
        /* Calculate error */
        scaling_constant = k;
        e = calc_texel_squared_error(trainingset);

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

    /* Print result */
    printf("\nK=%.3f, e=%.5f (%.2f%%)\n",
           best_k, lowest_e, sqrt(lowest_e)*100.0);

    /* Clean up */
    free(workers);
    free_trainingset(trainingset);
    free_tuningset(tuningset);
    destroy_game_state(state);
}

static void tune_parameters(char *training_file, char *parameter_file,
                            int nthreads, enum optimization_algorithm optalgo,
                            int niterations)
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

    /* Optimize the tuning set */
    switch (optalgo) {
    case OPT_LOCAL_SEARCH:
        local_search(tuningset, trainingset);
        break;
    case OPT_ADAM:
        adam(tuningset, trainingset, niterations, STEP_SIZE);
        break;
    default:
        assert(false);
        break;
    }

    /* Write tuning result */
    printf("\n");
    printf("Parameter values:\n");
    tuning_param_write_parameters(stdout, tuningset->params, true, false);
    fp = fopen(TUNING_FINAL_RESULT_FILE, "w");
    if (fp != NULL) {
        tuning_param_write_parameters(fp, tuningset->params, true, false);
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
    free(workers);
    free_tuningset(tuningset);
    free_trainingset(trainingset);
    destroy_game_state(state);
}

static void print_parameters(char *output_file, bool zero)
{
    FILE                *fp;
    struct tuning_param *params;

    fp = fopen(output_file, "w");
    if (fp == NULL) {
        printf("Failed to open output file\n");
        return;
    }

    params = tuning_param_create_list();
    tuning_param_write_parameters(fp, params, false, zero);

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
        score = eval_evaluate(&state->pos);

        /* Generate a trace for this function */
        trace = malloc(sizeof(struct eval_trace));
        eval_generate_trace(&state->pos, trace);

        /* Setup an equation and evaluate it */
        setup_eval_equation(trace, &trainingset->positions[k].equation);
        score2 = evaluate_equation(&trainingset->positions[k].equation,
                                   tuningset);
        score2 = (state->pos.stm == WHITE)?score2:-score2;
        free(trace);
        trace = NULL;

        /*
         * Check that the scores match. Since the standard evaluation
         * is done using integers and equations are evaluated using
         * doubles a difference of 1 is allowed to account for difference
         * in precision.
         */
        if (abs(score-score2) > 1) {
            printf("Wrong score (%d): %d (%d)\n", k, score2, score);
            printf("%s", trainingset->positions[k].epd);
            print_equation(&trainingset->positions[k].equation);
            printf("\n");
        }
    }

    /* Clean up */
    free_trainingset(trainingset);
    free_tuningset(tuningset);
    destroy_game_state(state);
}

static void print_error(char *training_file, int nthreads)
{
    struct tuningset    *tuningset;
    struct trainingset  *trainingset;
    struct gamestate    *state;
    double              error;

    assert(training_file != NULL);

    /* Create game state */
    state = create_game_state();

    /* Read training set */
    trainingset = read_trainingset(state, training_file);
    if (trainingset == NULL) {
        printf("Error: failed to read training set\n");
        return;
    }

    /* Allocate a tuningset with current parameter values */
    tuningset = malloc(sizeof(struct tuningset));
    tuningset->params = tuning_param_create_list();
    tuningset->size = NUM_TUNING_PARAMS;
    tuningset->nactive = NUM_TUNING_PARAMS;

    /* Setup worker threads */
    nworkerthreads = nthreads;
    workers = malloc(sizeof(struct tuning_worker)*nthreads);
    init_workers(trainingset, tuningset);

    /* Make sure all training positions are covered */
    workers[nthreads-1].last_pos = trainingset->size - 1;

    /* Calculate error */
    trace_positions();
    tuning_param_assign_current(tuningset->params);
    error = calc_texel_squared_error(trainingset);
    printf("Error: %f\n", error);

    /* Clean up */
    free(workers);
    free_tuningset(tuningset);
    free_trainingset(trainingset);
    destroy_game_state(state);
}

static void print_usage(void)
{
    printf("Usage: tuner [options]\n");
    printf("Options:\n");
    printf("\t-k <training file>\n\tCalculate the tuning constant K\n\n");
    printf("\t-v <training file>\n\tVerify evaluation tracing\n\n");
    printf("\t-t <training file> <parameter file>\n\tTune parameters\n\n");
    printf("\t-e <training file>\n\tCalculate error\n\n");
    printf("\t-p <output file>\n\tPrint all tunable parameters\n\n");
    printf("\t-n <nthreads>\n\tThe number of threads to use\n\n");
    printf("\t-i <niterations>\n\tThe number of iterations to run\n\n");
    printf("\t-o [local|adam]\n\tOptimization algorithm to use for tuning\n\n");
    printf("\t-z\n\tPrint tuning parameters with all values set to zero\n\n");
    printf("\t-h\n\tDisplay this message\n\n");
}

int main(int argc, char *argv[])
{
    enum optimization_algorithm optalgo;
    int                         iter;
    int                         nconv;
    char                        *training_file;
    char                        *parameter_file;
    char                        *output_file;
    int                         nthreads;
    int                         command;
    bool                        zero_params;
    int                         niterations;

    /* Turn off buffering for I/O */
    setbuf(stdout, NULL);
    setbuf(stdin, NULL);

    /* Seed random number generator */
    srand((unsigned int)time(NULL));

    /* Initialize components */
    chess_data_init();
    bb_init();
    see_init();

    /* Initialize options */
    training_file = NULL;
    parameter_file = NULL;
    output_file = NULL;
    nthreads = 1;
    optalgo = OPT_ADAM;
    zero_params = false;
    niterations = DEFAULT_ITERATIONS;

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
        } else if (!strcmp(argv[iter], "-i")) {
            iter++;
            if (iter == argc) {
                printf("Invalid argument\n");
                print_usage();
                exit(1);
            }
            nconv = sscanf(argv[iter], "%u", &niterations);
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
        } else if (!strcmp(argv[iter], "-v")) {
            command = 3;
            iter++;
            if (iter == argc) {
                printf("Invalid argument\n");
                print_usage();
                exit(1);
            }
            training_file = argv[iter];
        } else if (!strcmp(argv[iter], "-z")) {
            zero_params = true;
        } else if (!strcmp(argv[iter], "-o")) {
            iter++;
            if (iter == argc) {
                printf("Invalid argument\n");
                print_usage();
                exit(1);
            }
            if (!strcmp(argv[iter], "local")) {
                optalgo = OPT_LOCAL_SEARCH;
            } else if (!strcmp(argv[iter], "adam")) {
                optalgo = OPT_ADAM;
            } else {
                printf("Invalid argument\n");
                print_usage();
                exit(1);
            }
        } else if (!strcmp(argv[iter], "-e")) {
            command = 4;
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
        tune_parameters(training_file, parameter_file, nthreads, optalgo,
                        niterations);
        break;
    case 2:
        print_parameters(output_file, zero_params);
        break;
    case 3:
        verify_trace(training_file);
        break;
    case 4:
        print_error(training_file, nthreads);
        break;
    default:
        print_usage();
        exit(1);
        break;
    }

    return 0;
}
