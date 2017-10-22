/*
 *  list.c
 *    Simple implementation of linked list data structure
 *****************************************************************************
 *  This file is part of Fågelmataren, an embedded project created to learn
 *  Linux and C. See <https://github.com/Linkaan/Fagelmatare>
 *  Copyright (C) 2015-2017 Linus Styrén
 *
 *  Fågelmataren is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the Licence, or
 *  (at your option) any later version.
 *
 *  Fågelmataren is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public Licence for more details.
 *
 *  You should have received a copy of the GNU General Public Licence
 *  along with Fågelmataren.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 */

#include <stdio.h>
#include <stdlib.h>

#include "list.h"

int list_insert (llist *head, void *value)
{
    while (*head)
        head = &head[0]->next;

    *head = malloc (sizeof **head);
    if (!*head)
      	return -1;

    **head = (struct node){0, value};
    return 0;
}

int list_pop (llist *head, void **value)
{	
	struct node *next_head;

	if (*head == NULL)
		return -1;
	
	if (value)
		*value = head[0]->value;

	next_head = head[0]->next;
	free(*head);
	*head = next_head;

	return 0;
}

int list_remove (llist *head, void *value)
{
	struct node *prev, *curr;

	if (*head == NULL)
		return -1;

	if (head[0]->value == value)
		return list_pop (head, NULL);

	prev = curr = head[0]->next;
	while (curr)
	  {
	  	if (curr->value == value)
	  	  {
	  	  	prev->next = curr->next;
	  	  	free (curr);
	  	  	return 0;
	  	  }
	  	  prev = curr;
	  	  curr = curr->next;
	  }
	return -1;
}