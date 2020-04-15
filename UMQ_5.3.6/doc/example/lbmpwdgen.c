/*
"lbmpwdgen.c: application that generates password files for SRP authentication.

  Copyright (c) 2005-2014 Informatica Corporation  Permission is granted to licensees to use
  or alter this software for any purpose, including commercial applications,
  according to the terms laid out in the Software License Agreement.

  This source code example is provided by Informatica for educational
  and evaluation purposes only.

  THE SOFTWARE IS PROVIDED "AS IS" AND INFORMATICA DISCLAIMS ALL WARRANTIES
  EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY IMPLIED WARRANTIES OF
  NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR
  PURPOSE.  INFORMATICA DOES NOT WARRANT THAT USE OF THE SOFTWARE WILL BE
  UNINTERRUPTED OR ERROR-FREE.  INFORMATICA SHALL NOT, UNDER ANY CIRCUMSTANCES, BE
  LIABLE TO LICENSEE FOR LOST PROFITS, CONSEQUENTIAL, INCIDENTAL, SPECIAL OR
  INDIRECT DAMAGES ARISING OUT OF OR RELATED TO THIS AGREEMENT OR THE
  TRANSACTIONS CONTEMPLATED HEREUNDER, EVEN IF INFORMATICA HAS BEEN APPRISED OF
  THE LIKELIHOOD OF SUCH DAMAGES.
*/

#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#if defined(_WIN32)
/* Windows-only includes */
#include <winsock2.h>
#define SLEEP(s) Sleep((s)*1000)
#else
/* Unix-only includes */
#include <stdlib.h>
#include <unistd.h>
#define SLEEP(s) sleep(s)
#endif

#include "lbm/lbm.h"
#include "lbmpwdgen.h"

#define MAX_INPUT_LEN	(128)
#define MAX_INPUT_SECTIONS	(16)
#define MAX_SECTION_LEN		(64)
#define MAX_OUTPUT_LEN	(128)
#define MAX_CMD_LEN (16)

const char prompt[] = "pg>";
const char banner[] =
	"Password Generator. "
	"Generating password files for SRP authentication.\n";

static int flag_fileopened = 0;

typedef int (*cmd_func_t)(char *);
typedef struct _cmd_func_map_
{
	char cmd1[MAX_CMD_LEN];
	char cmd2[MAX_CMD_LEN];
	cmd_func_t process_cmd;
	char help[MAX_OUTPUT_LEN];
} cmd_func_map_t;

static int open_file(char*args);
static int close_file(char*args);
static int add_user(char*args);
static int del_user(char*args);
static int del_user_role(char*args);
static int add_role(char*args);
static int help(char*args);
static int quit(char*args);

const cmd_func_map_t cmd_func_map_list[] = {\
	{"open", "file", open_file, "open file   open pasword file, e.g. open file temp.xml"},
	{"close", "", close_file, "close       close password file, e.g. close"},
	{"add", "user", add_user, "add user    add a user with its name, password and role, e.g. add user userAbc pass1234 admin"},
	{"del", "user", del_user, "del user    delete the user entry in the password file, e.g. del user userAbc"},
	{"del", "urole", del_user_role, "del urole   delete the role from the user's role list, e.g. del urole user admin"},
	{"add", "role", add_role, "add role    add a role with its permitted action, e.g. add role admin MSG_LIST"},
	{"help", "", help, "help        display the usage"},
	{"quit", "", quit, "quit        quit the command shell"}
};
int num_cmds = sizeof(cmd_func_map_list)/sizeof(cmd_func_map_t);

char purpose[] = "Generating password files for SRP authentication.";
char usage[] =
"Usage: lbmpwdgen\n"
"  After the command prompt, pg>, enter one of the commands listed blow:\n"
"    open file     open pasword file, e.g. open file temp.xml.\n"
"    close         close password file, e.g. close.\n"
"    add user      add a user with its name, password and role, e.g. add user userAbc pass1234 admin.\n"
"    del user      delete the user entry in the password file, e.g. del user userAbc.\n"
"    del urole     delete the role from the user's role list, e.g. del urole user admin.\n"
"    add role      add a role with its permitted action, e.g. add role admin MSG_LIST.\n"
"    help          display the usage.\n"
"    quit          quit the command shell.\n"
;

int open_file(char*args)
{
	char buf[MAX_SECTION_LEN];
	FILE *fptemp;

	if(strlen(args) <= 0) {
		printf("Enter file name:");
		fgets(buf, MAX_SECTION_LEN, stdin);
		if(strlen(buf) <= 0) {
			printf(">>>warning: invalid file name.\n");
			return 0;
		}
		args = buf;
	}

	if((fptemp = fopen(args, "r")) == NULL){
		FILE *fp;
		if((fp=fopen(args, "a")) == NULL) {
			printf("<<< warning: password file can not be created.\n");
			return 0;
		}
		fprintf(fp,"%s\n",file_template);
		fclose(fp);
	}
	else
		fclose(fptemp);

	if(lbm_authstorage_open_storage_xml(args) < 0)
		printf("<<< warning: password file can not be opened.\n");
	else 
		flag_fileopened = 1;

	return 0;
}

int close_file(char*args)
{	
	lbm_authstorage_close_storage_xml();
	flag_fileopened = 0;

	return 0;
}

int add_user(char*args)
{
	char name[MAX_SECTION_LEN], pass[MAX_SECTION_LEN], role[MAX_SECTION_LEN];

	if(!args) printf("<<< warning: invalid user \n");

	if(sscanf(args,"%s %s %s", name, pass, role) < 3) {
		printf("<<< warning: invalid input. See help.\n");
		return 0;
	}

	if(lbm_authstorage_addtpnam(name, pass, 1) < 0) {
		printf("<<< warning: add user failed.\n");
		return 0;
	}

	if(lbm_authstorage_user_add_role(name, role) < 0)
		printf("<<< warning: add user role failed.\n");

	return 0;
}

int del_user(char*args)
{
	char name[MAX_SECTION_LEN];

	if(!args) printf("<<< warning: invalid user \n");

	if(sscanf(args,"%s", name) < 1) {
		printf("<<< warning: invalid input. See help.\n");
		return 0;
	}

	if(lbm_authstorage_deltpnam(name) < 0) {
		printf("<<< warning: del user failed.\n");
		return 0;
	}

	return 0;
}

int del_user_role(char*args)
{
	char name[MAX_SECTION_LEN], role[MAX_SECTION_LEN];

	if(!args) printf("<<< warning: invalid input parameter.\n");

	if(sscanf(args,"%s %s", name, role) < 2) {
		printf("<<< warning: invalid input. See help.\n");
		return 0;
	}

	if(lbm_authstorage_user_del_role(name, role) < 0) {
		printf("<<< warning: del user role failed.\n");
		return 0;
	}

	return 0;
}

int add_role(char*args)
{
	char action[MAX_SECTION_LEN], role[MAX_SECTION_LEN];

	if(!args) printf("<<< warning: invalid role. \n");

	if(sscanf(args,"%s %s", role, action) < 2) {
		printf("<<< warning: invalid input. See help.\n");
		return 0;
	}

	if(lbm_authstorage_roletable_add_role_action(role, action) < 0)
		printf("<<< warning: add role failed.\n");
	return 0;
}

int help(char*args)
{
	int i;

	for (i = 0; i < num_cmds; i++)
		printf("%s\n", cmd_func_map_list[i].help);

	return 0;
}

int quit(char*args)
{	
	return 0;
}

int parse_buff(char* buff, char** cmd1, char **cmd2, char **args)
{
	char *cptr;
	int i=0, bufflen;

	if(!buff)
		return -1;

	*cmd1 = *cmd2 = *args = "";
	bufflen = strlen(buff);

	/* remove the new line character */
	if((cptr = strchr(buff,'\n')))
		*cptr='\0';

	if(strlen(buff)<=0)
		return 0;

	/* search the first non-space char */
	while(buff[i]==' ')i++;
	if(i >= bufflen)
		return 0;

	*cmd1 = &buff[i];

	/* search the nexe space char */
	while((buff[i] != ' ')&& (i<bufflen))i++;
	if(i >= bufflen)
		return 0;

	buff[i++] = '\0';

	/* search the next non-space char */
	while(buff[i]==' ')i++;
	if(i >= bufflen)
		return 0;

	*cmd2 = &buff[i];

	/* search the nexe space char */
	while((buff[i] != ' ')&& (i<bufflen))i++;
	if(i >= bufflen)
		return 0;

	buff[i++] = '\0';


	/* search the next non-space char */
	while(buff[i]==' ')i++;
	if(i >= bufflen)
		return 0;

	*args = &buff[i];

	return 0;
}

int main(int argc, char *argv[])
{
	int flag_termination = 0, i;
	char buff[MAX_INPUT_LEN], *cmd1, *cmd2, *args;

	/* print out the usage information and exit*/
	if((argc >= 2) && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))
	{
		printf("%s\n%s\n%s", argv[0], purpose, usage);
		return 0;
	}
		

	printf("%s\n", banner);
	printf("%s\n\n",lbm_version());
	printf("%s\n", "Enter \"help\" for the usage.\n");

	while(!flag_termination)
	{
		printf("%s", prompt);
		fgets(buff, MAX_INPUT_LEN, stdin);
		parse_buff(buff, &cmd1, &cmd2, &args);

		/* exit on "quit" */
		if(strcmp(cmd1, "quit") == 0){
			flag_termination = 1;
			continue;
		}

		/* help */
		if(strcmp(cmd1, "help") == 0){
			help(NULL);
			continue;
		}

		if((!flag_fileopened) && (strcmp(cmd1, "open") != 0)) {
			printf(">>> warning: MUST open file first.\n");
			continue;
		}
		
		/* search command processing function */
		for (i = 0; i < num_cmds; i++)
		{
			/* if 1st part matchs */
			if( strcmp(cmd_func_map_list[i].cmd1, cmd1) == 0) {
				/* if 2nd part matches, break*/
				if(strcmp(cmd_func_map_list[i].cmd2, cmd2) == 0)								
					break;				
			}
		}

		/* if cmd matches, call func */	
		if(i < num_cmds) {
			cmd_func_map_list[i].process_cmd(args);
		}
		/* else, warning on unknown command */
		else
			printf(">>> unknown command.\n");
	}
	return 0;
}
