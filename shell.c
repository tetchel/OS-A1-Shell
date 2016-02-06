/**
    *	Shell.c: A simple unix shell emulator written for CS3305.
    *	Author: Tim Etchells
**/

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define COMMAND_LEN 1024    //MAXIMUM command length
#define MAX_TOK	    32	    //MAX number of tokens per command$
#define HIST_MAX    10	    //number of items to hold in history

//whether or not user has pressed ctrl+c (starts false)
volatile int term_requested = 0;

//return name of user who invoked the shell
//adapted from http://stackoverflow.com/questions/8953424/how-to-get-the-username-in-c-c-in-linux
const char* get_username() {
    uid_t uid = geteuid();		    //returns effective userid of current user
    struct passwd* pw = getpwuid(uid);	    //get passwd for that user
    const char* retval = pw->pw_name;	    //get username from the passwd

    //return if valid result, else perror and return ""
    if (retval)
        return retval;
    //should not happen, but just in case
    else {
        perror("Error getting username");
        return "jdoe";
    }
}

//splits a string into an array of substrings, delimited by spaces
int make_tokenlist(char *in, char *tokens[]) {
    char* line;
    char buf[COMMAND_LEN];
    strcpy(buf, in);
    int i = 0;

    line = buf;
    tokens[i] = strtok(line, " ");
    do  {
       i++;
       line = NULL;
       tokens[i] = strtok(line, " ");
    } while(tokens[i] != NULL);

    return i;
}

//execute a command without redirection or pipes
void exec_normal(char** tokens) {
    pid_t pid = fork();
    if(pid > 0) {
        //parent (shell)
        wait(NULL);
    }
    else if (pid < 0) {
        perror("Forking error! Aborting");
        return;
    }
    else {
        //child
        execvp(tokens[0], tokens);
        //this is an error because exec should have replaced the below code
        perror("Exec error");
	exit(EXIT_FAILURE); 
    }
}

//"override" of dup2 to handle error checking more cleanly
void dup2_(int first, int second) {
    if(dup2(first, second) < 0) {
	perror("Dupe error! Aborting");
	exit(EXIT_FAILURE);
    }
}

//execute a command with redirection (one or two way)
void exec_redir(char** tokens, int no_tokens, int command_type, int no_commands, int command_indices[]) {
    //quick validation
    if((command_type == 3 || command_type == 4) && no_commands <= 2) {
	printf("Not a valid command, please try again.\n");
	return;
    }

    fflush(0);
    //second and third items (file names)    
    char *second, *third;
    
    asprintf(&second, "%s", tokens[command_indices[1]]);
    //if there is a third item, get it too
    if(command_type == 3 || command_type == 4)
	asprintf(&third, "%s", tokens[command_indices[2]]);
    //null-terminate tokens so that it now only contains the command
    //and can be used with execvp.
    tokens[command_indices[1]-1] = 0;

    pid_t pid;
    if((pid = fork()) < 0) {
	perror("Fork error! Aborting");
	return;
    }
    else if(pid > 0) {
	//parent
	wait(NULL);
    }
    else {
	//child, perform the command
	if(command_type == 1 || command_type == 3) {
	    int input = open(second, O_RDONLY);
	    dup2_(input, STDIN_FILENO);
	    close(input);
	    if(command_type == 3) {
		int output = open(third, O_WRONLY | O_CREAT, 0666);
		dup2_(output, STDOUT_FILENO);
		close(output);
	    }
	    execvp(tokens[0], tokens);
	    perror("Exec error");
	    exit(EXIT_FAILURE);
	}
	else if(command_type == 2 || command_type == 4) {
	    int output = open(second, O_WRONLY | O_CREAT, 0666);
	    dup2_(output, STDOUT_FILENO);
	    close(output);
	    if(command_type == 4) {
		int input = open(third, O_RDONLY);
		dup2_(input, STDIN_FILENO);
		close(input);
	    }
	    execvp(tokens[0], tokens);
	    perror("Exec error");
	    exit(EXIT_FAILURE);
	}
    }
}

//start a piped process, passing the file descriptors for in/out, and the command to run
void start_piped_process(int in_fd, int out_fd, char** command) {
    pid_t pid;

    if((pid = fork()) < 0) {
	//error
	perror("Fork error! Aborting");
	return;
    }
    else if(pid == 0) {
	//child
	//move stdin to in_fd
	if(in_fd != STDIN_FILENO) {
	    dup2_(in_fd, STDIN_FILENO);
	    close(in_fd);
	}
	//move stdout to out_fd
	if(out_fd != STDOUT_FILENO) {
	    dup2_(out_fd, STDOUT_FILENO);
	    close(out_fd);
	}
	execvp(command[0], command);
	perror("Exec error! Aborting");
	exit(EXIT_FAILURE);
    }
    //the parent process is not needed
}

//extract command as an array from tokens, from indices start ... end
char** extract_command(char** tokens, int start, int end) {
    char** command = malloc(COMMAND_LEN);
    int j, k = 1;
    //first item
    asprintf(&command[0], "%s", tokens[start]);
    for(j = start+1; j < end; j++) {
	asprintf(&command[k], "%s", tokens[j]);
	k++;
    }
    //terminate so it works with execvp
    command[k] = '\0';
    return command;
}

//execute a command with any number of pipes
void exec_pipe(char** tokens, int no_tokens, int no_commands, int command_indices[]) {
    pid_t pid;
    int in_fd, i, fd[2];
    
    if((pid = fork()) < 0) {
	perror("Fork error! Aborting");
	exit(EXIT_FAILURE);
    }
    else if(pid > 0) {
	//parent
	wait(NULL);
    }
    else {
	//child
	//the first command will get input from stdin
	in_fd = STDIN_FILENO;

	char** command;
	for(i = 0; i < no_commands-1; i++) {
	    //build command from tokens using command_indices
	    command = extract_command(tokens, command_indices[i], command_indices[i+1]-1);
	    //set up the pipe
	    pipe(fd);
	    start_piped_process(in_fd, fd[1], command);
	    //close the pipe'd fd
	    close(fd[1]);
	    //the next process will use this as its input
	    in_fd = fd[0];
	}
	//last process must read from second-to-last process's pipe
	if(in_fd != STDIN_FILENO)
	    dup2(in_fd, STDIN_FILENO);
	
	//execute the last command (outputs to stdout)
	command = extract_command(tokens, command_indices[no_commands-1], no_tokens);
	execvp(command[0], command);
	perror("Exec error! Aborting");
	exit(EXIT_FAILURE);	
    }
}

//SIGTERM handler
void terminate(int signum) {
    term_requested = 1;
}

char* update_history(char* history, char* input, int* hist_size) {
    //check if deletion of oldest history is required
    if(*hist_size >= HIST_MAX) {
        //delete up to the first \n
        char* new_history = strstr(history, "\n");
        //add 1 to delete the first \n
        asprintf(&history, "%s", new_history+1);
    }
    //once hist_size reaches HIST_MAX, it just stays there
    else if (*hist_size <= HIST_MAX) {
        (*hist_size)++;
    }
    //add to history
    if(*hist_size > 1) {
        //append the newest history item
        asprintf(&history, "%s\n%s", history, input);
    }
    else {
        //first item
       asprintf(&history, "%s", input);
    }
    return history;
}

//main function, contains IO loop, handles history functionality, does some analysis of user input
//to determine which function to call, and which parameters to give
//all exec work is done by other functions
int main() {
    //SIGTERM handling code
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = terminate;
    sigaction(SIGTERM, &action, NULL);
    //end term handler

    //Start by obtaining username for prompt
    const char* username = get_username();
    //history is stored as one long string delimited by \n
    char* history;
    //once it reaches HIST_SIZE, start replacing old entries
    int hist_size = 0;

    //indices at which commands start
    int command_indices[MAX_TOK] = {0};
    //current index in the above array (as elements are added)
    //the 0th command starts is at tokens[0], this is already known
    int command_counter = 1;

    //main program loop
    //exit condition is lower down, if "exit" is entered or term_requested == true
    while(1) {
        //allocate for user input
        char* input = calloc(COMMAND_LEN, 0);
        if(!input) {
	    perror("Malloc error! Aborting");
	    exit(EXIT_FAILURE);
	}    
        //prompt
        printf("%s> ", username);
        //get input
        fgets(input, COMMAND_LEN, stdin);
	//chop off the newline
	input[strlen(input)-1] = '\0';
	
        //if input is not empty
	if(strlen(input) > 0)
	    history = update_history(history, input, &hist_size);
	else
	    continue;

        //if they entered "history", output it now that it is up-to-date
        if(!strcmp(input, "history")) {
            printf("%s\n", history);
        }
        //if they entered "exit" or sent SIGTERM, exit
        else if(!strcmp(input, "exit") || term_requested) {
            printf("Exit requested\n");
	    return 0;
	}
        //else, it's a command, analyze to figure out what to do next
        else {
            //tokenize input to determine what kind of command we have
            int num_tokens, i, command_type = 0;
            char* tokens[MAX_TOK];
            num_tokens = make_tokenlist(input, tokens);

            for(i = 0; i < num_tokens; i++) {
                //check for invalid command flag
                if(command_type == -1)
                    break;
                //loop through tokens looking for redirection or pipe
                if(!strcmp(tokens[i], "<")) {
                    command_indices[command_counter++] = i+1;
		    printf("A command starts at %d\n", command_indices[command_counter-1]);
                    if(command_type == 1 || command_type > 4)
                        //this indicates that there are two '<'s or a pipe, which is not valid
                        command_type = -1;
                    else if(command_type == 2)
                        //this indicates that the command has > followed by <
                        command_type = 4;
                    else
                        command_type = 1;
                }
                else if(!strcmp(tokens[i], ">")) {
                    command_indices[command_counter++] = i+1;
		    printf("A command starts at %d\n", command_indices[command_counter-1]);
		    if(command_type == 2 || command_type > 4)
                        //two '>'s or pipe, not valid
                         command_type = -1;
                    else if(command_type == 1)
                        // < followed by >
                        command_type = 3;
                    else
                        command_type = 2;
                }
                else if(!strcmp(tokens[i], "|")) {
                    command_indices[command_counter++] = i+1;
                    //one or more pipes
                    if(command_type >= 2 && command_type <= 4)
                        command_type = -1;
                    else
                        command_type = 5;
                }
            }
            if(command_type == -1)
                printf("Not a valid command, please try again.\n");
            else if(command_type == 0)
                exec_normal(tokens);
            else if(command_type >= 1 && command_type <= 4)
                exec_redir(tokens, num_tokens, command_type, command_counter, command_indices);
            else if(command_type == 5)
                exec_pipe(tokens, num_tokens, command_counter, command_indices);
        }   //end command execution block
    }   //end MAIN LOOP
    return 0;
}

