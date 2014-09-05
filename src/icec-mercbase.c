/*
 * IMC2 - an inter-mud communications protocol
 *
 * icec-mercbase.c: IMC-channel-extensions (ICE) Merc-specific client code
 *
 * Copyright (C) 1997 Oliver Jowett <oliver@jowett.manawatu.planet.co.nz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see the file COPYING); if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#if defined(macintosh)
#include <types.h>
#else
#include <sys/types.h>
#include <sys/time.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#define IN_IMC

#include "imc.h"
#include "imc-mercbase.h"
#include "icec.h"
#include "icec-mercbase.h"

static ice_channel *saved_channel_list;

void icec_save_channels(void) {
    ice_channel *c;
    FILE *fp;
    char name[MAX_STRING_LENGTH];
    strcpy(name, imc_prefix);
    strcat(name, "icec");
    fp = fopen(name, "w");
    if(!fp) {
        imc_logerror("Can't write to %s", name);
        return;
    }
    for(c = saved_channel_list; c; c = c->next) {
        /* update */
        ice_channel *current = icec_findlchannel(c->local->name);
        if(current) {
            imc_strfree(c->name);
            imc_strfree(c->local->format1);
            imc_strfree(c->local->format2);
            c->name = imc_strdup(current->name);
            c->local->format1 = imc_strdup(current->local->format1);
            c->local->format2 = imc_strdup(current->local->format2);
            c->local->level = current->local->level;
        }
        /* save */
        fprintf(fp,
                "%s %s %d\n"
                "%s\n"
                "%s\n",
                c->name, c->local->name, c->local->level,
                c->local->format1,
                c->local->format2);
    }
    fclose(fp);
}

void icec_load_channels(void) {
    FILE *fp;
    char name[MAX_STRING_LENGTH];
    char buf1[MAX_STRING_LENGTH];
    char buf2[MAX_STRING_LENGTH];
    char buf3[MAX_STRING_LENGTH];
    char buf4[MAX_STRING_LENGTH];
    int l;
    strcpy(name, imc_prefix);
    strcat(name, "icec");
    fp = fopen(name, "r");
    if(!fp) {
        imc_logerror("Can't open %s", name);
        return;
    }
    while(fscanf(fp,
                 "%s %s %d\n"
                 "%[^\n]\n"
                 "%[^\n]\n", buf1, buf2, &l, buf3, buf4) == 5) {
        ice_channel *c = imc_malloc(sizeof(*c));
        c->local = imc_malloc(sizeof(*c->local));
        c->name = imc_strdup(buf1);
        c->local->name = imc_strdup(buf2);
        c->local->format1 = imc_strdup(buf3);
        c->local->format2 = imc_strdup(buf4);
        c->local->level = l;
        c->next = saved_channel_list;
        saved_channel_list = c;
        imc_logstring("ICEc: configured %s as %s",
                      c->name, c->local->name);
    }
    fclose(fp);
}

ice_channel *icec_findlchannel(const char *name) {
    ice_channel *c;
    for(c = icec_channel_list; c; c = c->next)
        if(c->local && !str_cmp(c->local->name, name)) {
            return c;
        }
    return NULL;
}

void icec_localfree(ice_channel *c) {
    if(c->local) {
        imc_strfree(c->local->name);
        imc_strfree(c->local->format1);
        imc_strfree(c->local->format2);
        imc_free(c->local, sizeof(icec_lchannel));
        c->local = NULL;
    }
}

/* need exactly 2 %s's, and no other format specifiers */
static bool verify_format(const char *fmt) {
    const char *c;
    int i = 0;
    c = fmt;
    while((c = strchr(c, '%')) != NULL) {
        if(*(c + 1) == '%') { /* %% */
            c += 2;
            continue;
        }
        if(*(c + 1) != 's') { /* not %s */
            return FALSE;
        }
        c++;
        i++;
    }
    if(i != 2) {
        return FALSE;
    }
    return TRUE;
}

DEFINE_DO_FUN(do_icommand) {
#ifdef CIRCLE
    skip_spaces(&argument);
#endif
    send_to_char(icec_command(ch->name, argument), ch);
    send_to_char("\n\r", ch);
}

DEFINE_DO_FUN(do_isetup) {
    char cmd[IMC_NAME_LENGTH];
    char chan[IMC_NAME_LENGTH];
    char arg1[IMC_DATA_LENGTH];
    ice_channel *c;
    const char *a, *a1;
#ifdef CIRCLE
    skip_spaces(&argument);
#endif
    a = imc_getarg(argument, cmd, IMC_NAME_LENGTH);
    a = a1 = imc_getarg(a, chan, IMC_NAME_LENGTH);
    a = imc_getarg(a, arg1, IMC_DATA_LENGTH);
    if(!cmd[0] || !chan[0]) {
        send_to_char("Syntax: isetup <command> <channel> [<data..>]\n\r", ch);
        return;
    }
    c = icec_findchannel(chan);
    if(!c) {
        c = icec_findlchannel(chan);
        if(!c) {
            send_to_char("Unknown channel.\n\r", ch);
            return;
        }
    }
    if(!strcasecmp(cmd, "add")) {
        char buf[IMC_DATA_LENGTH];
        ice_channel *newc;
        if(c->local) {
            send_to_char("Channel is already active.\n\r", ch);
            return;
        }
        if(!arg1[0]) {
            send_to_char("Syntax: isetup add <channel> <local name>\n\r", ch);
            return;
        }
        c->local = imc_malloc(sizeof(*c->local));
        c->local->name = imc_strdup(arg1);
        c->local->level = LEVEL_IMMORTAL;
        sprintf(buf, "(%s) %%s: %%s", arg1);
        c->local->format1 = imc_strdup(buf);
        sprintf(buf, "(%s) %%s %%s", arg1);
        c->local->format2 = imc_strdup(buf);
        imc_addname(&ch->pcdata->ice_listen, c->local->name);
        newc = imc_malloc(sizeof(*newc));
        newc->name = imc_strdup(c->name);
        newc->local = imc_malloc(sizeof(*newc->local));
        newc->local->name = imc_strdup(arg1);
        newc->local->level = LEVEL_IMMORTAL;
        newc->local->format1 = imc_strdup(c->local->format1);
        newc->local->format2 = imc_strdup(c->local->format2);
        newc->next = saved_channel_list;
        saved_channel_list = newc;
        send_to_char("Channel added.\n\r", ch);
        icec_save_channels();
        return;
    }
    if(!c->local) {
        send_to_char("Channel is not locally configured, use 'isetup add' first.\n\r", ch);
        return;
    }
    if(!strcasecmp(cmd, "delete")) {
        ice_channel *saved, **last;
        for(last = &saved_channel_list, saved = *last; saved; saved = *last) {
            if(!strcasecmp(saved->local->name, c->local->name)) {
                icec_localfree(saved);
                imc_strfree(saved->name);
                *last = saved->next;
                imc_free(saved, sizeof(*saved));
            } else {
                last = &saved->next;
            }
        }
        icec_localfree(c);
        send_to_char("Channel is no longer locally configured.\n\r", ch);
        icec_save_channels();
        return;
    }
    if(!strcasecmp(cmd, "rename")) {
        if(!arg1[0]) {
            send_to_char("Syntax: isetup <channel> rename <newname>\n\r", ch);
            return;
        }
        if(icec_findlchannel(arg1)) {
            send_to_char("New channel name already exists.\n\r", ch);
            return;
        }
        imc_strfree(c->local->name);
        c->local->name = imc_strdup(arg1);
        send_to_char("Renamed ok.\n\r", ch);
        icec_save_channels();
        return;
    }
    if(!strcasecmp(cmd, "format1")) {
        if(!a1[0]) {
            send_to_char("Syntax: isetup <channel> format1 <string>\n\r", ch);
            return;
        }
        if(!verify_format(a1)) {
            send_to_char("Bad format - must contain exactly 2 %s's.\n\r", ch);
            return;
        }
        imc_strfree(c->local->format1);
        c->local->format1 = imc_strdup(a1);
        send_to_char("Format1 changed.\n\r", ch);
        icec_save_channels();
        return;
    }
    if(!strcasecmp(cmd, "format2")) {
        if(!a1[0]) {
            send_to_char("Syntax: isetup <channel> format2 <string>\n\r", ch);
            return;
        }
        if(!verify_format(a1)) {
            send_to_char("Bad format - must contain exactly 2 %s's.\n\r", ch);
            return;
        }
        imc_strfree(c->local->format2);
        c->local->format2 = imc_strdup(a1);
        send_to_char("Format2 changed.\n\r", ch);
        icec_save_channels();
        return;
    }
    if(!strcasecmp(cmd, "level")) {
        if(!arg1[0]) {
            send_to_char("Syntax: isetup <channel> level <level>\n\r", ch);
            return;
        }
        if(atoi(arg1) <= 0) {
            send_to_char("Positive level, please.\n\r", ch);
            return;
        }
        c->local->level = atoi(arg1);
        send_to_char("Level changed.\n\r", ch);
        icec_save_channels();
        return;
    }
    send_to_char("Unknown command.\n\r"
                 "Available commands: add delete rename format1 format2 level.\n\r", ch);
}

DEFINE_DO_FUN(do_ilist) {
    char buf[MAX_STRING_LENGTH];
    ice_channel *c;
    if(argument[0]) {
        c = icec_findlchannel(argument);
        if(!c) {
            c = icec_findchannel(argument);
        }
        if(!c) {
            send_to_char("No such channel.\n\r", ch);
            return;
        }
        sprintf(buf,
                "Channel %s:\n"
                "  Local name: %s\n"
                "  Format 1:   %s\n"
                "  Format 2:   %s\n"
                "  Level:      %d\n"
                "\n"
                "  Policy:     %s\n"
                "  Owner:      %s\n"
                "  Operators:  %s\n"
                "  Invited:    %s\n"
                "  Excluded:   %s\n",
                c->name,
                c->local ? c->local->name : "",
                c->local ? c->local->format1 : "",
                c->local ? c->local->format2 : "",
                c->local ? c->local->level : 0,
                c->policy == ICE_OPEN ? "open" :
                c->policy == ICE_CLOSED ? "closed" : "private",
                c->owner,
                c->operators,
                c->invited,
                c->excluded);
        send_to_char(buf, ch);
        return;
    }
    sprintf(buf, "%-15s %-15s %-15s %s\n\r", "Name", "Local name", "Owner", "Policy");
    for(c = icec_channel_list; c; c = c->next) {
        sprintf(buf + strlen(buf), "%-15s %-15s %-15s %s\n\r",
                c->name,
                c->local ? c->local->name : "(not local)",
                c->owner,
                c->policy == ICE_OPEN ? "open" :
                c->policy == ICE_CLOSED ? "closed" : "private");
    }
    for(c = saved_channel_list; c; c = c->next) {
        if(!icec_findchannel(c->name)) {
            sprintf(buf + strlen(buf), "%-15s %-15s %-15s %-7s  (inactive)\n\r",
                    c->name,
                    c->local ? c->local->name : "(not local)",
                    "", "");
        }
    }
    page_to_char(buf, ch);
}

DEFINE_DO_FUN(do_ichannels) {
    char arg[MAX_STRING_LENGTH];
    argument = one_argument(argument, arg);
    if(!arg[0]) {
        send_to_char("Currently tuned into:\n\r", ch);
        send_to_char(ch->pcdata->ice_listen, ch);
        send_to_char("\n\r", ch);
        return;
    }
    smash_tilde(arg);
    if(imc_hasname(ch->pcdata->ice_listen, arg)) {
        imc_removename(&ch->pcdata->ice_listen, arg);
        send_to_char("Removed.\n\r", ch);
    } else {
        if(!icec_findlchannel(arg)) {
            send_to_char("No such channel configured locally.\n\r", ch);
            return;
        }
        imc_addname(&ch->pcdata->ice_listen, arg);
        send_to_char("Added.\n\r", ch);
    }
}

void icec_showchannel(ice_channel *c, const char *from, const char *txt, int emote) {
    DESCRIPTOR_DATA *d, *dnext;
    CHAR_DATA *ch;
    char buf[MAX_STRING_LENGTH];
    if(!c->local) {
        return;
    }
    sprintf(buf, emote ? c->local->format2 : c->local->format1, from, color_itom(txt));
    strcat(buf, "\n\r");
    for(d = descriptor_list; d; d = dnext) {
        dnext = d->next;
        ch = d->original ? d->original : d->character;
        if(!ch ||
                IS_NPC(ch) ||
                get_trust(ch) < c->local->level ||
                !ice_audible(c, imc_makename(ch->name, imc_name)) ||
                !imc_hasname(ch->pcdata->ice_listen, c->local->name)) {
            continue;
        }
        send_to_char(buf, ch);
    }
}

/* check for icec channels, return TRUE to stop command processing, FALSE otherwise */
bool icec_command_hook(CHAR_DATA *ch, const char *command, const char *argument) {
    ice_channel *c;
    char arg[MAX_STRING_LENGTH];
    const char *word2;
    int emote = 0;
    if(IS_NPC(ch)) {
        return FALSE;
    }
#ifdef CIRCLE
    skip_spaces(&argument);
#endif
    c = icec_findlchannel(command);
    if(!c || !c->local) {
        return FALSE;
    }
    if(c->local->level > get_trust(ch)) {
        return FALSE;
    }
    if(!imc_hasname(ch->pcdata->ice_listen, c->local->name)) {
        return FALSE;
    }
    if(!ice_audible(c, imc_makename(ch->name, imc_name))) {
        send_to_char("You cannot use that channel.\n\r", ch);
        return TRUE;
    }
    word2 = imc_getarg(argument, arg, MAX_STRING_LENGTH);
    if(!arg[0]) {
        send_to_char("Use ichan to toggle the channel - or provide some text.\n\r", ch);
        return TRUE;
    }
    if(!str_cmp(arg, ",") ||
            !str_cmp(arg, "emote")) {
        emote = 1;
        argument = word2;
    }
    icec_sendmessage(c, ch->name, color_mtoi(argument), emote);
    return TRUE;
}

void icec_notify_update(ice_channel *c) {
    if(!c->local) {
        /* saved channel? */
        ice_channel *saved;
        for(saved = saved_channel_list; saved; saved = saved->next)
            if(!strcasecmp(saved->name, c->name)) {
                c->local = imc_malloc(sizeof(icec_lchannel));
                c->local->name = imc_strdup(saved->local->name);
                c->local->format1 = imc_strdup(saved->local->format1);
                c->local->format2 = imc_strdup(saved->local->format2);
                c->local->level = saved->local->level;
                return;
            }
    }
}
