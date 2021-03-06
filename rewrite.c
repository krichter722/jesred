/*
 * Modified: Ulises Nicolini by Vianet 2015/05/11
 * email: developers@sawcache.com
 * web:http://sawcache.com
 * Version: 3.0
 * 
 * $Id: rewrite.c,v 1.3 1998/08/15 00:01:14 elkner Exp $
 *
 * Author:  Squirm derived      http://www.senet.com.au/squirm/
 * Project: Jesred       http://ivs.cs.uni-magdeburg.de/~elkner/webtools/jesred/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * http://www.gnu.org/copyleft/gpl.html or ./gpl.html
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Thanks to Chris Foote, chris@senet.com.au - except parse_buff
 * not much to change here (i.e. don't like to go deeper into the pattern stuff)
 * ;-)
 *
 */

#include<stdio.h>
#include<strings.h>
#include<ctype.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>

#ifdef LOCAL_REGEX
#include "regex.h"
#else
#include<regex.h>
#endif

#include "log.h"
#include "pattern_list.h"
#include "rewrite.h"
#include "main.h"

/* load the stdin for the redirector into an IN_BUFF structure 
   Sets in_buff.url to "" if the fields can't be converted */

int replace_string(pattern_item *, char *, char *);
#ifdef USE_ACCEL
static int match_accel(char *, char *, int, int);
#endif

int
parse_buff(char *buff, char **ch_id, char **url)
{
    int c, i;
    struct in_addr address;
    char *token, *new_token;
    char *end[5];
    
    c = 0;
    new_token = strchr(buff,' ');
    if (new_token) {
        c++;
        *new_token='\0';
        end[0]=new_token;
        *ch_id=buff;
        token = strchr(++new_token,' ');
    if ( token ) {       /* URL */
        c++;
        *token = '\0';
        end[1] = token;
        *url = new_token;
        new_token = strchr(++token,' ');
        
    }
    }
   
        if(c != 2) {

	  for(i = 1; i < c; i++) {
            if ( end[i] )
                *end[i] = ' ';
	  }

	  log(ERROR, "incorrect input (%d): %s\n", c, buff);

        return 1;
	}
#ifdef DEBUG
    log(DEBG, "Request: %s %s\n", *ch_id, *url);
#endif    
    
    
    return 0;
}




/* returns replacement URL for a match in newurl
   < 0 if abort pattern match, 0 if no match found, > 1 pattern match
   if match, the number of the matching rule will be returned */
int
pattern_compare(char *url,char *newurl, pattern_item *phead)
{
    pattern_item *curr;
    int pos;
    int len;
    int i;
    int matched;
    int pattern_no = 0;
    curr = NULL;
  
    for(curr = phead; curr != NULL; curr = curr->next) {
	pattern_no++;
	matched = 1;
	/* assume a match until a character isn't the same */
	if(curr->type == ABORT) {
	    len = strlen(curr->pattern);
	    pos = strlen(url) - len; /* this is dangerous */
	    for(i = 0; i <= len; i++) {
		if (url[pos] != curr->pattern[i]) {
		    matched = 0;
		    break;
		}
		pos++;
	    }
	    if(matched) {
#ifdef DEBUG
		log(DEBG, "abort pattern matched: %s (rule %d)\n",
		    url, pattern_no);
#endif		
		return (0 - pattern_no); /* URL matches abort file extension */
	    }
	}
	else { 
	    /* check for accelerator string */
#ifdef USE_ACCEL
	    if(curr->has_accel) {
		/* check to see if the accelerator string matches, then bother
		   doing a regexec() on it */
		if(match_accel(url, curr->accel,
			       curr->accel_type, 
			       curr->case_sensitive)) {
#ifdef DEBUG
		    log(DEBG, "URL %s matches accelerator %s (rule %d)\n",
			url, curr->accel, pattern_no);
#endif		    
		    /* Now we must test for normal or extended */
		    if (curr->type == EXTENDED) {
			if ( replace_string(curr, url, newurl) == 1 )
			    return pattern_no;
		    }
		    else /* Type == NORMAL */ {
			if(regexec(&curr->cpattern, url, 0, 0, 0) == 0){
			    strcpy(newurl,curr->replacement);
			    return pattern_no;
			}
		    }
		} /* end match_accel loop */
	    }
	    else {
		/* we haven't got an accelerator string, so we use regex
		   instead */
		/* Now we must test for normal or extended */
#endif
		if (curr->type == EXTENDED) {
		    if ( replace_string(curr, url, newurl) == 1)
			return pattern_no;
		}
		else /* Type == NORMAL */ {
		    if(regexec(&curr->cpattern, url, 0, 0, 0) == 0) {
			strcpy(newurl,curr->replacement);
			return pattern_no;
		    }
		}
#ifdef USE_ACCEL
	    }
#endif
	}
    }
    return 0;
}
    
int
replace_string (pattern_item *curr, char *url, char *buffer)
{
    char *replacement_string = NULL;
    regmatch_t match_data[10];
    int parenthesis;
    char *in_ptr;
    char *out_ptr;
    int replay_num;
    int count;
   
    /* Perform the regex call */
    if (regexec (&curr->cpattern, url, 10, &match_data[0], 0) != 0)
	return 0;
  
    /* Ok, setup the traversal pointers */
    in_ptr = curr->replacement;
    out_ptr = buffer;
  
    /* Count the number of replays in the pattern */
    parenthesis = count_parenthesis (curr->pattern);
    if (parenthesis < 0) {
	/* Invalid return value - don't log because we already have done it */
	return 0;
    }
  
    /* Traverse the url string now */
    while (*in_ptr != '\0') {
	if (isdigit (*in_ptr)) {
	    /* We have a number, how many chars are there before us? */
	    switch (in_ptr - curr->replacement) {
		case 0:
		    /* This is the first char
		       Since there is no backslash before hand, this is not
		       a pattern match, so loop around */
		    {
			*out_ptr = *in_ptr;
			out_ptr++;
			in_ptr++;
			continue;
		    }
		case 1:
		    /* Only one char back to check, so see if it's a backslash */
		    if (*(in_ptr - 1) != '\\') {
			*out_ptr = *in_ptr;
			out_ptr++;
			in_ptr++;
			continue;
		    }
		    break;
		default:
		    /* Two or more chars back to check, so see if the previous is
		       a backslash, and also the one before. Two backslashes mean
		       that we should not replace anything! */
		    if ( (*(in_ptr - 1) != '\\') ||
			 ((*(in_ptr - 1) == '\\') && (*(in_ptr - 2) == '\\')) ) {
			*out_ptr = *in_ptr;
			out_ptr++;
			in_ptr++;
			continue;
		    }
	    }
	    
	    /* Ok, if we reach this point, then we have found something to
	       replace. It also means that the last time we went through here,
	       we copied in a backslash char, so we should backtrack one on
	       the output string before continuing */
	    out_ptr--;
	    
	    /* We need to convert the current in_ptr into a number for array
	       lookups */
	    replay_num = (*in_ptr) - '0';
	    
	    /* Now copy in the chars from the replay string */
	    for (count = match_data[replay_num].rm_so; 
		 count < match_data[replay_num].rm_eo; count++) {
		/* Copy in the chars */
		*out_ptr = url[count];
		out_ptr++;
	    }
      
	    /* Increment the in pointer */
	    in_ptr++;
	} else {
	    *out_ptr = *in_ptr;
	    out_ptr++;
	    in_ptr++;
	}
	
	/* Increment the in pointer and loop around */
	/* in_ptr++; */
    }
  
    /* Terminate the string */
    *out_ptr = '\0';
    
    /* return to the caller (buffer contains the new url) */
    return 1;
}

#ifdef USE_ACCEL
static int
match_accel(char *url, char *accel, int accel_type, int case_sensitive)
{
    /* return 1 if url contains accel */
    int i, offset;
    static char l_accel[BUFSIZE];
    int accel_len;
    int url_len;
  
    if(accel_type == ACCEL_NORMAL) {
	if(case_sensitive) {
	    if(strstr(url, accel))
		return 1;
	    else
		return 0;
	}
	else {
	    /* convert to lower case */
	    for(i = 0; url[i] != '\0'; i++)
		l_accel[i] = tolower(url[i]);
	    l_accel[i] = '\0';   
	    if(strstr(l_accel, accel))
		return 1;
	    else
		return 0;
	}
    }
    if(accel_type == ACCEL_START) {
	accel_len = strlen(accel);
	url_len = strlen(url);
	if(url_len < accel_len)
	    return 0;
	if(case_sensitive) {
	    for(i = 0; i < accel_len; i++) {
		if(accel[i] != url[i])
		    return 0;
	    }
	}
	else {
	    for(i = 0; i < accel_len; i++) {
		if(accel[i] != tolower(url[i]))
		    return 0;
	    }
	}
	return 1;
    }
    if(accel_type == ACCEL_END) {
	accel_len = strlen(accel);
	url_len = strlen(url);
	offset = url_len - accel_len;
	if(offset < 0)
	    return 0;
	if(case_sensitive) {
	    for(i = 0; i < accel_len; i++) {
		if(accel[i] != url[i+offset])
		    return 0;
	    }
	}
	else {
	    for(i = 0; i < accel_len; i++) {
		if(accel[i] != tolower(url[i+offset]))
		    return 0;
	    }
	}
	return 1;
    }
  
    /* we shouldn't reach this section! */
    return 0;
}
#endif






