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
#ifndef POLYBOOK_H
#define POLYBOOK_H

#include "types.h"

/*
 * Open the opening book.
 *
 * @param path The path of the opening book.
 * @return Returns true if the book was sucessfully opened.
 */
bool polybook_open(char *path);

/*
 * Close the opening book.
 */
void polybook_close(void);

/*
 * Try to find a move to play in the opening book.
 *
 * @param pos The current position.
 * @return Returns the move to play.
 */
uint32_t polybook_probe(struct position *pos);

/*
 * Get a list of all book entries for the current position.
 *
 * @param pos The current position.
 * @param nentries Set to the number of book entries in the list.
 * @return Returns a list of book moves. The returned list should be
 *         freed by the caller.
 */
struct book_entry* polybook_get_entries(struct position *pos, int *nentries);

#endif
