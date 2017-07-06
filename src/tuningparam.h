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
#ifndef TUNINGPARAM_H
#define TUNINGPARAM_H

#include <stdio.h>

#include "chess.h"

/* The total number of tunable parameters */
#define NUM_TUNING_PARAMS 478

/* Declaration for a tuning parameter */
struct param_decl {
    char *name;
    int  start;
    int  stop;
    int  min;
    int  max;
};

/* Tunable parameter */
struct tuning_param {
    bool active;
    int  current;
    int  min;
    int  max;
};

/*
 * Assign the current values for all tuning paramateres to the
 * corresponding evaluation parameter.
 *
 * @param params List of tunable parameters.
 */
void tuning_param_assign_current(struct tuning_param *params);

/*
 * Create a list of all tunable parameters.
 *
 * @param nparams Location to store the number of parameters at.
 * @return Returns a list of all tuning parameters.
 */
struct tuning_param* tuning_param_create_list(void);

/*
 * Destroy a list of tuning parameters.
 *
 * @params The list to destroy.
 */
void tuning_param_destroy_list(struct tuning_param *params);

/*
 * Get the declaration for a tuning parameter.
 *
 * @param name The name of the parameter.
 * @return Returns the parameter declaration.
 */
struct param_decl* tuning_param_lookup(char *name);

/*
 * Write the current values for the specified tuning parameters.
 *
 * @param fp The location to write the parameters to.
 * @param params The tuning parameters to write.
 * @param active_only Flag indicating if only actice parameters should be
 *                    printed.
 */
void tuning_param_write_parameters(FILE *fp, struct tuning_param *params,
                                   bool active_only);

#endif
